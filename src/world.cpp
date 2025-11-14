/**
 * @file world.cpp
 * @brief Voxel world management with chunk generation, culling, and rendering
 *
 * This file implements the World class which manages:
 * - Procedural chunk generation in a 3D grid
 * - View frustum culling for efficient rendering
 * - Distance-based culling with hysteresis
 * - Block placement and removal
 * - Chunk mesh updates and Vulkan buffer management
 * - Coordinate system conversions (world ↔ chunk ↔ local)
 *
 * Created by original author
 */

#include "world.h"
#include "world_utils.h"
#include "world_constants.h"
#include "terrain_constants.h"
#include "vulkan_renderer.h"
#include "frustum.h"
#include "debug_state.h"
#include "logger.h"
#include "block_system.h"
#include "biome_system.h"
#include "tree_generator.h"
#include <glm/glm.hpp>
#include <thread>
#include <algorithm>
#include <cmath>
#include <unordered_set>

// ========== WORLD GENERATION CONFIGURATION ==========

/**
 * @brief Enable natural ocean generation
 *
 * When enabled, areas where terrain is significantly below water level become oceans.
 * Oceans are hardcoded (not YAML-based) and consist of water blocks with sand/stone floors.
 * This creates natural breaks in biome generation, making the world feel more realistic.
 *
 * Ocean threshold: Terrain height < (WATER_LEVEL - OCEAN_DEPTH_THRESHOLD) = ocean
 */
constexpr bool ENABLE_NATURAL_OCEANS = true;
constexpr int OCEAN_DEPTH_THRESHOLD = 8;  // Blocks below water level to trigger ocean

// ====================================================

World::World(int width, int height, int depth, int seed)
    : m_width(width), m_height(height), m_depth(depth), m_seed(seed) {
    // Center world generation around origin (0, 0, 0)
    int halfWidth = width / 2;
    int halfDepth = depth / 2;

    Logger::info() << "Creating world with " << width << "x" << height << "x" << depth << " chunks (seed: " << seed << ")";
    Logger::info() << "Chunk coordinates range: X[" << -halfWidth << " to " << (width - halfWidth - 1)
                   << "], Y[0 to " << (height - 1) << "], Z[" << -halfDepth << " to " << (depth - halfDepth - 1) << "]";

    // Reserve space for chunks
    m_chunks.reserve(width * height * depth);

    for (int x = -halfWidth; x < width - halfWidth; ++x) {
        for (int y = 0; y < height; ++y) {
            for (int z = -halfDepth; z < depth - halfDepth; ++z) {
                // Create chunk and store in hash map for O(1) lookup
                auto chunk = std::make_unique<Chunk>(x, y, z);
                Chunk* chunkPtr = chunk.get();
                m_chunkMap[ChunkCoord{x, y, z}] = std::move(chunk);

                // Also store raw pointer in vector for fast iteration
                m_chunks.push_back(chunkPtr);
            }
        }
    }

    Logger::info() << "Total chunks created: " << m_chunks.size();

    // Validate that biomes are loaded before creating world
    if (BiomeRegistry::getInstance().getBiomeCount() == 0) {
        throw std::runtime_error("BiomeRegistry is empty! Call BiomeRegistry::loadBiomes() before creating a World.");
    }

    // Initialize biome map with seed
    m_biomeMap = std::make_unique<BiomeMap>(seed);
    Logger::info() << "Biome map initialized with " << BiomeRegistry::getInstance().getBiomeCount() << " biomes";

    // Initialize tree generator
    m_treeGenerator = std::make_unique<TreeGenerator>(seed);
    Logger::info() << "Tree generator initialized";

    // Generate tree templates for all biomes (each biome gets unique trees)
    BiomeRegistry::getInstance().generateTreeTemplates(m_treeGenerator.get());
    Logger::info() << "Generated unique tree templates for each biome";

    // Initialize water simulation and particle systems
    m_waterSimulation = std::make_unique<WaterSimulation>();
    m_particleSystem = std::make_unique<ParticleSystem>();

    Logger::info() << "Water simulation and particle systems initialized";
}

World::~World() {
    std::cout << "  Destroying World..." << std::endl;
    std::cout.flush();

    // unique_ptr in m_chunkMap automatically cleans up - no manual delete needed
    // m_chunks vector only contains non-owning pointers, so no cleanup needed
    // This could take time with many chunks (e.g., 128x3x128 = 49,152 chunks)

    std::cout << "  World destroyed (" << m_chunks.size() << " chunks)" << std::endl;
    std::cout.flush();
}

void World::generateWorld() {
    // Parallel chunk generation for better performance
    const unsigned int numThreads = std::thread::hardware_concurrency();
    const size_t chunksPerThread = (m_chunks.size() + numThreads - 1) / numThreads;

    std::vector<std::thread> threads;
    threads.reserve(numThreads);

    // Step 1: Generate terrain blocks in parallel
    BiomeMap* biomeMapPtr = m_biomeMap.get();
    for (unsigned int i = 0; i < numThreads; ++i) {
        size_t startIdx = i * chunksPerThread;
        size_t endIdx = std::min(startIdx + chunksPerThread, m_chunks.size());

        if (startIdx >= m_chunks.size()) break;

        threads.emplace_back([this, biomeMapPtr, startIdx, endIdx]() {
            for (size_t j = startIdx; j < endIdx; ++j) {
                m_chunks[j]->generate(biomeMapPtr);
            }
        });
    }

    // Wait for all threads to complete terrain generation
    for (auto& thread : threads) {
        thread.join();
    }

    // Step 2: Generate meshes for all chunks (must be done after ALL terrain is generated
    // so that neighbor checks work correctly across chunk boundaries)
    threads.clear();
    for (unsigned int i = 0; i < numThreads; ++i) {
        size_t startIdx = i * chunksPerThread;
        size_t endIdx = std::min(startIdx + chunksPerThread, m_chunks.size());

        if (startIdx >= m_chunks.size()) break;

        threads.emplace_back([this, startIdx, endIdx]() {
            for (size_t j = startIdx; j < endIdx; ++j) {
                m_chunks[j]->generateMesh(this);
            }
        });
    }

    // Wait for all threads to complete mesh generation
    for (auto& thread : threads) {
        thread.join();
    }
}

void World::decorateWorld() {
    using namespace TerrainGeneration;

    Logger::info() << "Starting world decoration (trees, vegetation)...";

    // Generate per-biome tree templates first (each biome gets 10 unique tree templates)
    // Use .get() to extract raw pointer from unique_ptr (safe borrowing - function doesn't store pointer)
    BiomeRegistry::getInstance().generateTreeTemplates(m_treeGenerator.get());

    int treesPlaced = 0;
    int undergroundFeaturesPlaced = 0;

    // Use offset seed for decoration to make it different from terrain
    std::mt19937 rng(m_seed + 77777);
    std::uniform_int_distribution<int> densityDist(0, 100);

    // Calculate world bounds
    int halfWidth = m_width / 2;
    int halfDepth = m_depth / 2;
    int maxWorldY = m_height * Chunk::HEIGHT;

    // Track modified chunks for selective mesh regeneration
    std::unordered_set<Chunk*> modifiedChunks;

    // Decorate surface - grid-based tree sampling for proper density
    // Sample every 4 blocks on a grid (8x8 = 64 sample points per chunk)
    // This gives proper Minecraft-accurate tree density in forests
    const int TREE_SAMPLE_SPACING = 4;  // Sample every 4 blocks

    for (int chunkX = -halfWidth; chunkX < m_width - halfWidth; ++chunkX) {
        for (int chunkZ = -halfDepth; chunkZ < m_depth - halfDepth; ++chunkZ) {
            // Grid-based sampling within chunk
            for (int localX = 0; localX < Chunk::WIDTH; localX += TREE_SAMPLE_SPACING) {
                for (int localZ = 0; localZ < Chunk::DEPTH; localZ += TREE_SAMPLE_SPACING) {
                    // Add small random offset (0-3 blocks) to avoid perfectly aligned trees
                    std::uniform_int_distribution<int> offsetDist(0, TREE_SAMPLE_SPACING - 1);
                    int offsetX = offsetDist(rng);
                    int offsetZ = offsetDist(rng);

                    int sampleX = std::min(localX + offsetX, Chunk::WIDTH - 1);
                    int sampleZ = std::min(localZ + offsetZ, Chunk::DEPTH - 1);

                    // Convert to world coordinates
                    float worldX = static_cast<float>(chunkX * Chunk::WIDTH + sampleX);
                    float worldZ = static_cast<float>(chunkZ * Chunk::DEPTH + sampleZ);

                // Get biome at this position
                const Biome* biome = m_biomeMap->getBiomeAt(worldX, worldZ);
                if (!biome || !biome->trees_spawn) continue;

                // Check tree density probability
                if (densityDist(rng) > biome->tree_density) continue;

                // Get terrain height directly from biome map (already calculated!)
                // This is 200x faster than searching from sky downward
                int terrainHeight = m_biomeMap->getTerrainHeightAt(worldX, worldZ);

                // Find actual solid ground near terrain height (verify surface)
                int groundY = terrainHeight;
                for (int y = terrainHeight; y >= std::max(0, terrainHeight - 5); y--) {
                    int blockID = getBlockAt(worldX, static_cast<float>(y), worldZ);
                    if (blockID != BLOCK_AIR && blockID != BLOCK_WATER) {
                        groundY = y;
                        break;
                    }
                }

                if (groundY < 10) continue;  // Too low for tree placement

                // placeTree expects block coordinates
                // Check for float-to-int overflow before casting
                float blockXf = worldX;
                float blockZf = worldZ;
                if (blockXf < INT_MIN || blockXf > INT_MAX || blockZf < INT_MIN || blockZf > INT_MAX) {
                    continue;  // Skip this tree - coordinates out of range
                }
                int blockX = static_cast<int>(blockXf);
                int blockZ = static_cast<int>(blockZf);

                // Place tree using biome's tree templates (each biome has unique trees)
                if (m_treeGenerator->placeTree(this, blockX, groundY + 1, blockZ, biome)) {
                    treesPlaced++;

                    // Track affected chunks (tree + neighbors) for mesh regeneration
                    // Trees can span multiple chunks, so mark the chunk and all adjacent chunks
                    Chunk* centerChunk = getChunkAtWorldPos(worldX, static_cast<float>(groundY + 1), worldZ);
                    if (centerChunk) {
                        modifiedChunks.insert(centerChunk);
                        // Add all 26 neighboring chunks (3x3x3 cube around tree)
                        for (int dx = -1; dx <= 1; dx++) {
                            for (int dy = -1; dy <= 1; dy++) {
                                for (int dz = -1; dz <= 1; dz++) {
                                    Chunk* neighbor = getChunkAt(centerChunk->getChunkX() + dx,
                                                                   centerChunk->getChunkY() + dy,
                                                                   centerChunk->getChunkZ() + dz);
                                    if (neighbor) modifiedChunks.insert(neighbor);
                                }
                            }
                        }
                    }
                }
                }  // End localZ loop
            }  // End localX loop
        }  // End chunkZ loop
    }  // End chunkX loop

    // Decorate underground biome chambers
    for (int chunkX = -halfWidth; chunkX < m_width - halfWidth; ++chunkX) {
        for (int chunkY = 0; chunkY < m_height; ++chunkY) {
            for (int chunkZ = -halfDepth; chunkZ < m_depth - halfDepth; ++chunkZ) {
                // Sample positions in underground chunks
                std::uniform_int_distribution<int> posDist(0, Chunk::WIDTH - 1);

                for (int attempt = 0; attempt < 2; attempt++) {  // 2 attempts per underground chunk
                    int localX = posDist(rng);
                    int localY = posDist(rng);
                    int localZ = posDist(rng);

                    int worldX = chunkX * Chunk::WIDTH + localX;
                    int worldY = chunkY * Chunk::HEIGHT + localY;
                    int worldZ = chunkZ * Chunk::DEPTH + localZ;

                    // Check if in underground chamber
                    if (m_biomeMap->isUndergroundBiomeAt(worldX, worldY, worldZ)) {
                        // Check if this is floor of chamber (solid block below, air here)
                        int blockHere = getBlockAt(worldX, worldY, worldZ);
                        int blockBelow = getBlockAt(worldX, static_cast<float>(worldY - 1), worldZ);

                        if (blockHere == BLOCK_AIR && blockBelow == BLOCK_STONE) {
                            // Get underground biome
                            const Biome* biome = m_biomeMap->getBiomeAt(worldX, worldZ);

                            // Place mushrooms or small features (for now, just count)
                            // TODO: Add mushrooms, glowing flora, etc.
                            undergroundFeaturesPlaced++;
                        }
                    }
                }
            }
        }
    }

    Logger::info() << "Decoration complete. Placed " << treesPlaced << " trees and "
                   << undergroundFeaturesPlaced << " underground features";

    // Batch regenerate meshes for modified chunks (parallel for performance)
    Logger::info() << "Regenerating meshes for " << modifiedChunks.size() << " modified chunks in parallel...";

    // Convert set to vector for parallel iteration
    std::vector<Chunk*> modifiedChunksVec(modifiedChunks.begin(), modifiedChunks.end());

    // Parallel mesh regeneration (same pattern as generateWorld)
    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 4;  // Fallback
    std::vector<std::thread> threads;
    threads.reserve(numThreads);

    size_t chunksPerThread = (modifiedChunksVec.size() + numThreads - 1) / numThreads;

    for (unsigned int i = 0; i < numThreads; ++i) {
        size_t startIdx = i * chunksPerThread;
        size_t endIdx = std::min(startIdx + chunksPerThread, modifiedChunksVec.size());

        if (startIdx >= modifiedChunksVec.size()) break;

        threads.emplace_back([this, &modifiedChunksVec, startIdx, endIdx]() {
            for (size_t idx = startIdx; idx < endIdx; ++idx) {
                modifiedChunksVec[idx]->generateMesh(this);
            }
        });
    }

    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }

    Logger::info() << "Regenerated " << modifiedChunksVec.size() << " chunk meshes in parallel (out of "
                   << m_chunks.size() << " total chunks)";
}

void World::createBuffers(VulkanRenderer* renderer) {
    // Only create buffers for chunks with vertices (skip empty chunks)
    for (auto& chunk : m_chunks) {
        if (chunk->getVertexCount() > 0) {
            chunk->createVertexBuffer(renderer);
        }
    }

    // PERFORMANCE FIX: After all buffer uploads are submitted, wait once for GPU to finish
    // This is much faster than waiting after each individual buffer upload (old approach)
    // With fence-based approach in endSingleTimeCommands(), this ensures all uploads complete
    // before we return from world initialization
    vkQueueWaitIdle(renderer->getGraphicsQueue());
}

void World::cleanup(VulkanRenderer* renderer) {
    // Destroy all chunk buffers before deleting chunks
    for (auto& chunk : m_chunks) {
        chunk->destroyBuffers(renderer);
    }
}

void World::renderWorld(VkCommandBuffer commandBuffer, const glm::vec3& cameraPos, const glm::mat4& viewProj, float renderDistance, VulkanRenderer* renderer) {
    // Validate camera position for NaN/Inf to prevent rendering errors
    if (!std::isfinite(cameraPos.x) || !std::isfinite(cameraPos.y) || !std::isfinite(cameraPos.z)) {
        return; // Skip rendering if camera position is invalid
    }

    // Extract frustum from view-projection matrix
    Frustum frustum = extractFrustum(viewProj);

    // Chunk culling: account for chunk size to prevent popping
    // Chunks are 32x32x32 blocks = 16x16x16 world units
    // Fragment shader discards at renderDistance * FRAGMENT_DISCARD_MARGIN (see shader.frag)
    // Render chunks if their farthest corner could be visible
    using namespace WorldConstants;
    const float fragmentDiscardDistance = renderDistance * FRAGMENT_DISCARD_MARGIN;
    const float renderDistanceWithMargin = fragmentDiscardDistance + CHUNK_HALF_DIAGONAL;
    const float renderDistanceSquared = renderDistanceWithMargin * renderDistanceWithMargin;

    // Frustum margin: add extra padding to prevent edge-case popping
    const float frustumMargin = CHUNK_HALF_DIAGONAL + FRUSTUM_CULLING_PADDING;

    int renderedCount = 0;
    int distanceCulled = 0;
    int frustumCulled = 0;

    // Store transparent chunks for second pass (sorted back-to-front)
    std::vector<std::pair<Chunk*, float>> transparentChunks;

    // ========== PASS 1: RENDER OPAQUE GEOMETRY ==========
    for (auto& chunk : m_chunks) {
        // Skip chunks with no opaque vertices
        if (chunk->getVertexCount() == 0) {
            // Still need to check for transparent geometry
            if (chunk->getTransparentVertexCount() > 0) {
                // Stage 1: Distance culling
                glm::vec3 delta = chunk->getCenter() - cameraPos;
                float distanceSquared = glm::dot(delta, delta);

                if (distanceSquared <= renderDistanceSquared) {
                    // Stage 2: Frustum culling
                    glm::vec3 chunkMin = chunk->getMin();
                    glm::vec3 chunkMax = chunk->getMax();

                    if (frustumAABBIntersect(frustum, chunkMin, chunkMax, frustumMargin)) {
                        transparentChunks.push_back(std::make_pair(chunk, distanceSquared));
                    } else{
                        frustumCulled++;
                    }
                } else {
                    distanceCulled++;
                }
            }
            continue;
        }

        // Stage 1: Distance culling (fast, eliminates far chunks)
        glm::vec3 delta = chunk->getCenter() - cameraPos;
        float distanceSquared = glm::dot(delta, delta);

        if (distanceSquared > renderDistanceSquared) {
            distanceCulled++;
            continue;
        }

        // Stage 2: Frustum culling (catches chunks behind camera)
        // Get chunk AABB bounds
        glm::vec3 chunkMin = chunk->getMin();
        glm::vec3 chunkMax = chunk->getMax();

        if (!frustumAABBIntersect(frustum, chunkMin, chunkMax, frustumMargin)) {
            frustumCulled++;
            continue;
        }

        // Chunk passed all culling tests - render opaque geometry
        chunk->render(commandBuffer, false);  // false = opaque
        renderedCount++;

        // If chunk has transparent geometry, add to transparent list
        if (chunk->getTransparentVertexCount() > 0) {
            transparentChunks.push_back(std::make_pair(chunk, distanceSquared));
        }
    }

    // ========== PASS 2: RENDER TRANSPARENT GEOMETRY (SORTED) ==========
    if (!transparentChunks.empty() && renderer != nullptr) {
        // Bind transparent pipeline (depth test enabled, depth write disabled)
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->getTransparentPipeline());

        // IMPORTANT: Rebind descriptor sets (contains texture atlas)
        VkDescriptorSet descriptorSet = renderer->getCurrentDescriptorSet();
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                               renderer->getPipelineLayout(), 0, 1,
                               &descriptorSet, 0, nullptr);

        // Sort transparent chunks back-to-front (farthest first)
        std::sort(transparentChunks.begin(), transparentChunks.end(),
                  [](const auto& a, const auto& b) {
                      return a.second > b.second;  // Greater distance first
                  });

        // Render transparent chunks in sorted order
        for (const auto& pair : transparentChunks) {
            pair.first->render(commandBuffer, true);  // true = transparent
        }
    }

    // Store stats in DebugState for display
    DebugState::instance().chunksRendered = renderedCount;
    DebugState::instance().chunksDistanceCulled = distanceCulled;
    DebugState::instance().chunksFrustumCulled = frustumCulled;
    DebugState::instance().chunksTotalInWorld = static_cast<int>(m_chunks.size());

    // Debug output periodically (roughly once per second at 60 FPS)
    // Gated behind debug_world ConVar - use console command: "debug_world 1" to enable
    static int frameCount = 0;
    if (DebugState::instance().debugWorld.getValue() &&
        frameCount++ % WorldConstants::DEBUG_OUTPUT_INTERVAL == 0) {
        Logger::debug() << "Rendered: " << renderedCount << " chunks | "
                        << "Distance culled: " << distanceCulled << " | "
                        << "Frustum culled: " << frustumCulled << " | "
                        << "Total: " << m_chunks.size() << " chunks";
    }
}

Chunk* World::getChunkAtUnsafe(int chunkX, int chunkY, int chunkZ) {
    // UNSAFE: No locking - caller must hold m_chunkMapMutex
    // O(1) hash map lookup instead of O(n) linear search
    auto it = m_chunkMap.find(ChunkCoord{chunkX, chunkY, chunkZ});
    if (it != m_chunkMap.end()) {
        return it->second.get();
    }
    return nullptr;
}

Chunk* World::getChunkAt(int chunkX, int chunkY, int chunkZ) {
    // THREAD SAFETY: Shared lock for concurrent reads
    std::shared_lock<std::shared_mutex> lock(m_chunkMapMutex);
    return getChunkAtUnsafe(chunkX, chunkY, chunkZ);
}

int World::getBlockAt(float worldX, float worldY, float worldZ) {
    // Convert world coordinates to chunk and local block coordinates
    auto coords = worldToBlockCoords(worldX, worldY, worldZ);

    Chunk* chunk = getChunkAt(coords.chunkX, coords.chunkY, coords.chunkZ);
    if (chunk == nullptr) {
        return 0; // Air (outside world bounds)
    }

    return chunk->getBlock(coords.localX, coords.localY, coords.localZ);
}

Chunk* World::getChunkAtWorldPosUnsafe(float worldX, float worldY, float worldZ) {
    // UNSAFE: No locking - caller must hold m_chunkMapMutex
    auto coords = worldToBlockCoords(worldX, worldY, worldZ);
    return getChunkAtUnsafe(coords.chunkX, coords.chunkY, coords.chunkZ);
}

Chunk* World::getChunkAtWorldPos(float worldX, float worldY, float worldZ) {
    // Convert world coordinates to chunk coordinates
    auto coords = worldToBlockCoords(worldX, worldY, worldZ);
    return getChunkAt(coords.chunkX, coords.chunkY, coords.chunkZ);
}

bool World::addStreamedChunk(std::unique_ptr<Chunk> chunk) {
    if (!chunk) {
        return false;  // Null chunk
    }

    int chunkX = chunk->getChunkX();
    int chunkY = chunk->getChunkY();
    int chunkZ = chunk->getChunkZ();

    // Bounds checking
    int halfWidth = m_width / 2;
    int halfHeight = m_height / 2;
    int halfDepth = m_depth / 2;

    if (chunkX < -halfWidth || chunkX >= halfWidth ||
        chunkY < 0 || chunkY >= m_height ||
        chunkZ < -halfDepth || chunkZ >= halfDepth) {
        Logger::warn() << "Attempted to add out-of-bounds chunk (" << chunkX << ", " << chunkY << ", " << chunkZ << ")";
        return false;
    }

    // Thread-safe insertion
    std::unique_lock<std::shared_mutex> lock(m_chunkMapMutex);

    ChunkCoord coord{chunkX, chunkY, chunkZ};

    // Check for duplicates
    if (m_chunkMap.find(coord) != m_chunkMap.end()) {
        Logger::warn() << "Chunk (" << chunkX << ", " << chunkY << ", " << chunkZ << ") already exists, discarding streamed chunk";
        return false;
    }

    // Add to map
    Chunk* chunkPtr = chunk.get();
    m_chunkMap[coord] = std::move(chunk);
    m_chunks.push_back(chunkPtr);

    Logger::debug() << "Added streamed chunk (" << chunkX << ", " << chunkY << ", " << chunkZ << ") to world";

    return true;
}

int World::getBlockAtUnsafe(float worldX, float worldY, float worldZ) {
    // UNSAFE: No locking - caller must hold m_chunkMapMutex
    auto coords = worldToBlockCoords(worldX, worldY, worldZ);
    Chunk* chunk = getChunkAtUnsafe(coords.chunkX, coords.chunkY, coords.chunkZ);
    if (chunk == nullptr) {
        return 0; // Air (outside world bounds)
    }
    return chunk->getBlock(coords.localX, coords.localY, coords.localZ);
}

void World::setBlockAtUnsafe(float worldX, float worldY, float worldZ, int blockID) {
    // UNSAFE: No locking - caller must hold m_chunkMapMutex
    auto coords = worldToBlockCoords(worldX, worldY, worldZ);
    Chunk* chunk = getChunkAtUnsafe(coords.chunkX, coords.chunkY, coords.chunkZ);
    if (chunk == nullptr) {
        return; // Outside world bounds, do nothing
    }
    chunk->setBlock(coords.localX, coords.localY, coords.localZ, blockID);
}

uint8_t World::getBlockMetadataAtUnsafe(float worldX, float worldY, float worldZ) {
    // UNSAFE: No locking - caller must hold m_chunkMapMutex
    auto coords = worldToBlockCoords(worldX, worldY, worldZ);
    Chunk* chunk = getChunkAtUnsafe(coords.chunkX, coords.chunkY, coords.chunkZ);
    if (chunk == nullptr) {
        return 0; // Out of bounds
    }
    return chunk->getBlockMetadata(coords.localX, coords.localY, coords.localZ);
}

void World::setBlockMetadataAtUnsafe(float worldX, float worldY, float worldZ, uint8_t metadata) {
    // UNSAFE: No locking - caller must hold m_chunkMapMutex
    auto coords = worldToBlockCoords(worldX, worldY, worldZ);
    Chunk* chunk = getChunkAtUnsafe(coords.chunkX, coords.chunkY, coords.chunkZ);
    if (chunk == nullptr) {
        return; // Outside world bounds, do nothing
    }
    chunk->setBlockMetadata(coords.localX, coords.localY, coords.localZ, metadata);
}

void World::setBlockAt(float worldX, float worldY, float worldZ, int blockID, bool regenerateMesh) {
    // Convert world coordinates to chunk and local block coordinates
    auto coords = worldToBlockCoords(worldX, worldY, worldZ);

    Chunk* chunk = getChunkAt(coords.chunkX, coords.chunkY, coords.chunkZ);
    if (chunk == nullptr) {
        return; // Outside world bounds, do nothing
    }

    // Set the block
    chunk->setBlock(coords.localX, coords.localY, coords.localZ, blockID);

    // Optionally regenerate mesh immediately (disable for batch operations)
    if (regenerateMesh) {
        // NOTE: Block break animations could be added in the future by:
        // - Adding a callback parameter to this function
        // - Delaying mesh regeneration until animation completes
        // - Using a particle system for break effects
        // For now, blocks update instantly (Minecraft-style instant break)
        chunk->generateMesh(this);
        // Note: We don't call createBuffer here - that needs a renderer which we don't have access to
        // We'll mark the chunk as needing a buffer update elsewhere
    }
}

uint8_t World::getBlockMetadataAt(float worldX, float worldY, float worldZ) {
    // Convert world coordinates to chunk and local block coordinates
    auto coords = worldToBlockCoords(worldX, worldY, worldZ);

    Chunk* chunk = getChunkAt(coords.chunkX, coords.chunkY, coords.chunkZ);
    if (chunk == nullptr) {
        return 0; // Out of bounds
    }

    return chunk->getBlockMetadata(coords.localX, coords.localY, coords.localZ);
}

void World::setBlockMetadataAt(float worldX, float worldY, float worldZ, uint8_t metadata) {
    // Convert world coordinates to chunk and local block coordinates
    auto coords = worldToBlockCoords(worldX, worldY, worldZ);

    Chunk* chunk = getChunkAt(coords.chunkX, coords.chunkY, coords.chunkZ);
    if (chunk == nullptr) {
        return; // Outside world bounds, do nothing
    }

    // Set the metadata
    chunk->setBlockMetadata(coords.localX, coords.localY, coords.localZ, metadata);
}

void World::breakBlock(float worldX, float worldY, float worldZ, VulkanRenderer* renderer) {
    // THREAD SAFETY: Acquire unique lock for exclusive write access
    // This prevents race conditions when multiple threads break blocks simultaneously
    std::unique_lock<std::shared_mutex> lock(m_chunkMapMutex);

    // Check if block is water (use UNSAFE version - we already hold lock)
    int blockID = getBlockAtUnsafe(worldX, worldY, worldZ);
    auto& registry = BlockRegistry::instance();

    if (blockID != 0 && registry.get(blockID).isLiquid) {
        // Only allow breaking source blocks (level 0)
        uint8_t waterLevel = getBlockMetadataAtUnsafe(worldX, worldY, worldZ);
        if (waterLevel > 0) {
            // This is flowing water, can't break it directly
            return;
        }

        // It's a source block - break it and remove all connected flowing water
        setBlockAtUnsafe(worldX, worldY, worldZ, 0);
        setBlockMetadataAtUnsafe(worldX, worldY, worldZ, 0);

        // Flood fill to remove all connected flowing water (level > 0)
        std::vector<glm::vec3> toCheck;
        std::unordered_set<glm::ivec3> visited;

        toCheck.push_back(glm::vec3(worldX, worldY, worldZ));

        while (!toCheck.empty()) {
            glm::vec3 pos = toCheck.back();
            toCheck.pop_back();

            glm::ivec3 ipos(int(pos.x), int(pos.y), int(pos.z));
            if (visited.find(ipos) != visited.end()) continue;
            visited.insert(ipos);

            // Check 6 neighbors
            glm::vec3 neighbors[6] = {
                pos + glm::vec3(1.0f, 0.0f, 0.0f),
                pos + glm::vec3(-1.0f, 0.0f, 0.0f),
                pos + glm::vec3(0.0f, 1.0f, 0.0f),
                pos + glm::vec3(0.0f, -1.0f, 0.0f),
                pos + glm::vec3(0.0f, 0.0f, 1.0f),
                pos + glm::vec3(0.0f, 0.0f, -1.0f)
            };

            for (const auto& neighborPos : neighbors) {
                int neighborBlock = getBlockAtUnsafe(neighborPos.x, neighborPos.y, neighborPos.z);
                if (neighborBlock != 0 && registry.get(neighborBlock).isLiquid) {
                    uint8_t neighborLevel = getBlockMetadataAtUnsafe(neighborPos.x, neighborPos.y, neighborPos.z);
                    if (neighborLevel > 0) {  // It's flowing water
                        setBlockAtUnsafe(neighborPos.x, neighborPos.y, neighborPos.z, 0);
                        setBlockMetadataAtUnsafe(neighborPos.x, neighborPos.y, neighborPos.z, 0);
                        toCheck.push_back(neighborPos);
                    }
                }
            }
        }
    } else {
        // Normal block - just break it
        setBlockAtUnsafe(worldX, worldY, worldZ, 0);
    }

    // Update the affected chunk and all adjacent chunks
    // Must regenerate MESH (not just vertex buffer) because face culling needs updating
    Chunk* affectedChunk = getChunkAtWorldPosUnsafe(worldX, worldY, worldZ);
    if (affectedChunk) {
        try {
            affectedChunk->generateMesh(this);
            affectedChunk->createVertexBuffer(renderer);
        } catch (const std::exception& e) {
            Logger::error() << "Failed to update chunk after breaking block: " << e.what();
            // Mesh is already generated, just buffer creation failed
            // Chunk will still render with old buffer until next update
        }
    }

    // Always update all 6 adjacent chunks (not just on boundaries)
    // This handles cases like breaking grass revealing stone below
    // Note: Using unsafe version since we already hold unique_lock
    Chunk* neighbors[6] = {
        getChunkAtWorldPosUnsafe(worldX - 1.0f, worldY, worldZ),  // -X
        getChunkAtWorldPosUnsafe(worldX + 1.0f, worldY, worldZ),  // +X
        getChunkAtWorldPosUnsafe(worldX, worldY - 1.0f, worldZ),  // -Y (below)
        getChunkAtWorldPosUnsafe(worldX, worldY + 1.0f, worldZ),  // +Y (above)
        getChunkAtWorldPosUnsafe(worldX, worldY, worldZ - 1.0f),  // -Z
        getChunkAtWorldPosUnsafe(worldX, worldY, worldZ + 1.0f)   // +Z
    };

    // Regenerate mesh and buffer for each unique neighbor chunk
    for (int i = 0; i < 6; i++) {
        if (neighbors[i] && neighbors[i] != affectedChunk) {
            // Skip if already updated (same chunk)
            bool alreadyUpdated = false;
            for (int j = 0; j < i; j++) {
                if (neighbors[j] == neighbors[i]) {
                    alreadyUpdated = true;
                    break;
                }
            }
            if (!alreadyUpdated) {
                try {
                    neighbors[i]->generateMesh(this);
                    neighbors[i]->createVertexBuffer(renderer);
                } catch (const std::exception& e) {
                    Logger::error() << "Failed to update neighbor chunk: " << e.what();
                    // Continue updating other chunks even if one fails
                }
            }
        }
    }
}

void World::breakBlock(const glm::vec3& position, VulkanRenderer* renderer) {
    breakBlock(position.x, position.y, position.z, renderer);
}

void World::breakBlock(const glm::ivec3& coords, VulkanRenderer* renderer) {
    // Convert block coordinates to world coordinates
    float worldX = static_cast<float>(coords.x);
    float worldY = static_cast<float>(coords.y);
    float worldZ = static_cast<float>(coords.z);
    breakBlock(worldX, worldY, worldZ, renderer);
}

void World::placeBlock(float worldX, float worldY, float worldZ, int blockID, VulkanRenderer* renderer) {
    // THREAD SAFETY: Acquire unique lock for exclusive write access
    // This prevents race conditions when multiple threads place blocks simultaneously
    std::unique_lock<std::shared_mutex> lock(m_chunkMapMutex);

    // Don't place air blocks (use breakBlock for that)
    if (blockID <= 0) return;

    // Check if there's already a block here (don't place over existing blocks)
    // Use UNSAFE version - we already hold lock
    int existingBlock = getBlockAtUnsafe(worldX, worldY, worldZ);
    if (existingBlock != 0) return;

    // Place the block
    setBlockAtUnsafe(worldX, worldY, worldZ, blockID);

    // If placing water, set metadata to 0 (source block)
    auto& registry = BlockRegistry::instance();
    if (registry.get(blockID).isLiquid) {
        setBlockMetadataAtUnsafe(worldX, worldY, worldZ, 0);  // Level 0 = source block
    }

    // Update the affected chunk and all adjacent chunks
    // Must regenerate MESH (not just vertex buffer) because face culling needs updating
    Chunk* affectedChunk = getChunkAtWorldPosUnsafe(worldX, worldY, worldZ);
    if (affectedChunk) {
        try {
            affectedChunk->generateMesh(this);
            affectedChunk->createVertexBuffer(renderer);
        } catch (const std::exception& e) {
            Logger::error() << "Failed to update chunk after placing block: " << e.what();
            // Mesh is already generated, just buffer creation failed
            // Chunk will still render with old buffer until next update
        }
    }

    // Always update all 6 adjacent chunks (not just on boundaries)
    // Note: Using unsafe version since we already hold unique_lock
    Chunk* neighbors[6] = {
        getChunkAtWorldPosUnsafe(worldX - 1.0f, worldY, worldZ),  // -X
        getChunkAtWorldPosUnsafe(worldX + 1.0f, worldY, worldZ),  // +X
        getChunkAtWorldPosUnsafe(worldX, worldY - 1.0f, worldZ),  // -Y (below)
        getChunkAtWorldPosUnsafe(worldX, worldY + 1.0f, worldZ),  // +Y (above)
        getChunkAtWorldPosUnsafe(worldX, worldY, worldZ - 1.0f),  // -Z
        getChunkAtWorldPosUnsafe(worldX, worldY, worldZ + 1.0f)   // +Z
    };

    // Regenerate mesh and buffer for each unique neighbor chunk
    for (int i = 0; i < 6; i++) {
        if (neighbors[i] && neighbors[i] != affectedChunk) {
            // Skip if already updated (same chunk)
            bool alreadyUpdated = false;
            for (int j = 0; j < i; j++) {
                if (neighbors[j] == neighbors[i]) {
                    alreadyUpdated = true;
                    break;
                }
            }
            if (!alreadyUpdated) {
                try {
                    neighbors[i]->generateMesh(this);
                    neighbors[i]->createVertexBuffer(renderer);
                } catch (const std::exception& e) {
                    Logger::error() << "Failed to update neighbor chunk: " << e.what();
                    // Continue updating other chunks even if one fails
                }
            }
        }
    }
}

void World::placeBlock(const glm::vec3& position, int blockID, VulkanRenderer* renderer) {
    placeBlock(position.x, position.y, position.z, blockID, renderer);
}

void World::updateLiquids(VulkanRenderer* renderer) {
    // Simplified Minecraft-style water flow implementation
    // - Level 0: Source block (infinite water, doesn't disappear)
    // - Levels 1-7: Flowing water (spreads horizontally with level decay)
    // - Water flows down infinitely (becomes source block when falling)
    // - Horizontal spread: up to 7 blocks from source
    // TODO: Update rendering to show water height based on level (currently all water renders at full height)

    auto& registry = BlockRegistry::instance();
    std::unordered_set<Chunk*> chunksToUpdate;

    // Track water blocks that need updating
    struct WaterBlock {
        float x, y, z;
        uint8_t level;
    };
    std::vector<WaterBlock> waterToAdd;

    // Pass 1: Process all water blocks and schedule flows
    // Performance optimization: Only check chunks with water
    // TODO: Track water-containing chunks for even better performance
    for (int y = 0; y < m_height; ++y) {
        for (int x = 0; x < m_width; ++x) {
            for (int z = 0; z < m_depth; ++z) {
                int halfWidth = m_width / 2;
                int halfDepth = m_depth / 2;
                int chunkX = x - halfWidth;
                int chunkZ = z - halfDepth;

                Chunk* chunk = getChunkAt(chunkX, y, chunkZ);
                if (!chunk) continue;

                // Early skip: If chunk has no vertices, it's likely empty
                if (chunk->getVertexCount() == 0 && chunk->getTransparentVertexCount() == 0) continue;

                // Iterate through blocks in this chunk (top to bottom for proper flow)
                // Sample every other block for performance (still looks smooth)
                for (int localX = 0; localX < Chunk::WIDTH; localX += 1) {
                    for (int localY = Chunk::HEIGHT - 1; localY >= 0; localY -= 1) {
                        for (int localZ = 0; localZ < Chunk::DEPTH; localZ += 1) {
                            int blockID = chunk->getBlock(localX, localY, localZ);
                            if (blockID == 0) continue;

                            const BlockDefinition& def = registry.get(blockID);
                            if (!def.isLiquid) continue;

                            // Calculate world position
                            float worldX = static_cast<float>(chunkX * Chunk::WIDTH + localX);
                            float worldY = static_cast<float>(y * Chunk::HEIGHT + localY);
                            float worldZ = static_cast<float>(chunkZ * Chunk::DEPTH + localZ);

                            // Get water level
                            uint8_t waterLevel = getBlockMetadataAt(worldX, worldY, worldZ);

                            // VERTICAL FLOW: Water always flows down first
                            float belowY = worldY - 1.0f;
                            int blockBelow = getBlockAt(worldX, belowY, worldZ);

                            if (blockBelow == 0) {
                                // Air below - water flows down as SOURCE block (level 0)
                                // CRITICAL: In Minecraft, falling water becomes a source!
                                waterToAdd.push_back({worldX, belowY, worldZ, 0});
                                chunksToUpdate.insert(getChunkAtWorldPos(worldX, belowY, worldZ));
                                // Skip horizontal spread when falling (priority to vertical flow)
                                continue;
                            } else if (!registry.get(blockBelow).isLiquid) {
                                // Solid block below - try horizontal spread
                                // Only spread if we're a source or low-level flow (level < 7)
                                if (waterLevel < 7) {
                                    uint8_t newLevel = waterLevel + 1;

                                    // Check 4 horizontal neighbors
                                    const glm::vec3 directions[4] = {
                                        {0.5f, 0.0f, 0.0f},   // +X
                                        {-0.5f, 0.0f, 0.0f},  // -X
                                        {0.0f, 0.0f, 0.5f},   // +Z
                                        {0.0f, 0.0f, -0.5f}   // -Z
                                    };

                                    for (const auto& dir : directions) {
                                        float nx = worldX + dir.x;
                                        float ny = worldY + dir.y;
                                        float nz = worldZ + dir.z;

                                        int neighborBlock = getBlockAt(nx, ny, nz);

                                        if (neighborBlock == 0) {
                                            // Empty space - place water
                                            waterToAdd.push_back({nx, ny, nz, newLevel});
                                            chunksToUpdate.insert(getChunkAtWorldPos(nx, ny, nz));
                                        } else if (registry.get(neighborBlock).isLiquid) {
                                            // Existing water - update if we have lower level (stronger flow)
                                            uint8_t neighborLevel = getBlockMetadataAt(nx, ny, nz);
                                            if (newLevel < neighborLevel) {
                                                setBlockMetadataAt(nx, ny, nz, newLevel);
                                                chunksToUpdate.insert(getChunkAtWorldPos(nx, ny, nz));
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Pass 2: Apply all changes
    for (const auto& water : waterToAdd) {
        int existing = getBlockAt(water.x, water.y, water.z);
        if (existing == 0) {
            // Only place if still empty
            setBlockAt(water.x, water.y, water.z, 5); // Water block ID
            setBlockMetadataAt(water.x, water.y, water.z, water.level);
        } else if (registry.get(existing).isLiquid) {
            // Update level if better (lower level = stronger flow)
            uint8_t existingLevel = getBlockMetadataAt(water.x, water.y, water.z);
            if (water.level < existingLevel) {
                setBlockMetadataAt(water.x, water.y, water.z, water.level);
            }
        }
    }

    // Pass 2.5: Evaporation - Remove water blocks at max level (edges)
    std::vector<glm::vec3> blocksToEvaporate;
    for (const auto& water : waterToAdd) {
        if (water.level >= 7) {  // Level 7 = edge water, evaporates
            int blockID = getBlockAt(water.x, water.y, water.z);
            if (blockID != 0 && registry.get(blockID).isLiquid) {
                uint8_t level = getBlockMetadataAt(water.x, water.y, water.z);
                if (level >= 7) {  // Double-check it's actually level 7
                    blocksToEvaporate.push_back(glm::vec3(water.x, water.y, water.z));
                    chunksToUpdate.insert(getChunkAtWorldPos(water.x, water.y, water.z));
                }
            }
        }
    }
    // Remove evaporated blocks
    for (const auto& pos : blocksToEvaporate) {
        setBlockAt(pos.x, pos.y, pos.z, 0);  // Remove block
        setBlockMetadataAt(pos.x, pos.y, pos.z, 0);  // Clear metadata
    }

    // Pass 3: Regenerate meshes for all affected chunks
    // Aggressively limit updates per frame for maximum FPS
    int updateCount = 0;
    const int maxChunkUpdates = 3;  // Max 3 chunks per update for best FPS
    for (Chunk* chunk : chunksToUpdate) {
        if (chunk && updateCount < maxChunkUpdates) {
            chunk->generateMesh(this);
            chunk->createVertexBuffer(renderer);
            updateCount++;
        }
    }
}

void World::updateWaterSimulation(float deltaTime, VulkanRenderer* renderer) {
    // Update particle system
    m_particleSystem->update(deltaTime);

    // Update water simulation
    m_waterSimulation->update(deltaTime, this);

    // Check for water level changes and spawn splash particles
    // TODO: Track water level changes in simulation and spawn particles

    // Regenerate meshes for chunks with active water
    const auto& activeChunks = m_waterSimulation->getActiveWaterChunks();

    // Limit chunk updates per frame to prevent lag spikes (max 5 per frame)
    int updatesThisFrame = 0;
    const int maxUpdatesPerFrame = 5;

    for (const auto& chunkPos : activeChunks) {
        if (updatesThisFrame >= maxUpdatesPerFrame) break;

        Chunk* chunk = getChunkAt(chunkPos.x, chunkPos.y, chunkPos.z);
        if (chunk) {
            // Only update if there's significant water activity
            // TODO: Add dirty flag system to track which chunks need updates
            chunk->generateMesh(this);
            chunk->createVertexBuffer(renderer);
            updatesThisFrame++;
        }
    }
}
