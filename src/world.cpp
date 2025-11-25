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
#include "world_streaming.h"
#include "terrain_constants.h"
#include "vulkan_renderer.h"
#include "frustum.h"
#include "debug_state.h"
#include "logger.h"
#include "block_system.h"
#include "biome_system.h"
#include "lighting_system.h"
#include "tree_generator.h"
#include <glm/glm.hpp>
#include <thread>
#include <future>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <filesystem>
#include <fstream>
#include <sstream>

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

World::World(int width, int height, int depth, int seed, float tempBias, float moistBias, float ageBias)
    : m_width(width), m_height(height), m_depth(depth), m_seed(seed),
      m_temperatureBias(tempBias), m_moistureBias(moistBias), m_ageBias(ageBias) {
    // Center world generation around origin (0, 0, 0)
    int halfWidth = width / 2;
    int halfHeight = height / 2;  // CHANGED: Center Y axis too for deep caves
    int halfDepth = depth / 2;

    Logger::info() << "Creating world with " << width << "x" << height << "x" << depth << " chunks (seed: " << seed << ")";
    Logger::info() << "Chunk coordinates range: X[" << -halfWidth << " to " << (width - halfWidth - 1)
                   << "], Y[" << -halfHeight << " to " << (height - halfHeight - 1)
                   << "], Z[" << -halfDepth << " to " << (depth - halfDepth - 1) << "]";

    // STREAMING OPTIMIZATION: Don't create all chunks at startup!
    // With 320 height, that's 12*320*12 = 46,080 chunks which takes forever to generate.
    // Instead, we'll create chunks on-demand using the WorldStreaming system.
    // The chunk map starts empty and chunks are added dynamically as the player explores.
    Logger::info() << "World initialized for streaming (chunks will be generated on-demand)";

    // Validate that biomes are loaded before creating world
    if (BiomeRegistry::getInstance().getBiomeCount() == 0) {
        throw std::runtime_error("BiomeRegistry is empty! Call BiomeRegistry::loadBiomes() before creating a World.");
    }

    // Initialize biome map with seed, biases, and biome temperature/moisture ranges
    // This ensures noise generation covers all biomes evenly
    auto& biomeRegistry = BiomeRegistry::getInstance();
    auto [minTemp, maxTemp] = biomeRegistry.getTemperatureRange();
    auto [minMoisture, maxMoisture] = biomeRegistry.getMoistureRange();

    m_biomeMap = std::make_unique<BiomeMap>(seed, tempBias, moistBias, ageBias,
                                             minTemp, maxTemp, minMoisture, maxMoisture);
    Logger::info() << "Biome map initialized with " << biomeRegistry.getBiomeCount() << " biomes "
                  << "(temp: " << minTemp << "-" << maxTemp << ", moisture: " << minMoisture << "-" << maxMoisture << ")";

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

    // Initialize lighting system
    m_lightingSystem = std::make_unique<LightingSystem>(this);
    Logger::info() << "Lighting system initialized";
}

World::~World() {
    Logger::info() << "Destroying World...";

    // unique_ptr in m_chunkMap automatically cleans up - no manual delete needed
    // m_chunks vector only contains non-owning pointers, so no cleanup needed
    // This could take time with many chunks (e.g., 128x3x128 = 49,152 chunks)

    Logger::info() << "World destroyed (" << m_chunks.size() << " chunks)";
}

void World::generateSpawnChunks(int centerChunkX, int centerChunkY, int centerChunkZ, int radius) {
    /**
     * OPTIMIZED SPAWN GENERATION (2025-11-25)
     *
     * Two-tier generation for faster streaming startup:
     * 1. Inner radius (spawn area): Full decoration + lighting + mesh
     * 2. Outer radius (terrain buffer): Terrain only, no decoration/mesh
     *
     * Two-thread generation for better parallelism:
     * - Surface thread: Generates Y >= 0 chunks (terrain + biomes)
     * - Underground thread: Generates Y < 0 chunks (caves + ores)
     */

    // Calculate outer radius for terrain-only pre-generation
    // Double the spawn radius for terrain buffer - player can wait for smoother gameplay
    const int TERRAIN_BUFFER_RADIUS = radius * 2;  // 2x spawn radius for terrain buffer

    Logger::info() << "Generating spawn chunks: inner=" << radius << " (decorated), outer="
                   << TERRAIN_BUFFER_RADIUS << " (terrain-only)";

    // Collect chunks to generate, split by surface/underground
    std::vector<Chunk*> surfaceChunks;      // Y >= 0
    std::vector<Chunk*> undergroundChunks;  // Y < 0
    std::vector<Chunk*> innerChunks;        // Within spawn radius (for decoration)

    // THREAD SAFETY: Acquire unique lock for chunk creation
    {
        std::unique_lock<std::shared_mutex> lock(m_chunkMapMutex);

        for (int dx = -TERRAIN_BUFFER_RADIUS; dx <= TERRAIN_BUFFER_RADIUS; dx++) {
            for (int dy = -TERRAIN_BUFFER_RADIUS; dy <= TERRAIN_BUFFER_RADIUS; dy++) {
                for (int dz = -TERRAIN_BUFFER_RADIUS; dz <= TERRAIN_BUFFER_RADIUS; dz++) {
                    int chunkX = centerChunkX + dx;
                    int chunkY = centerChunkY + dy;
                    int chunkZ = centerChunkZ + dz;

                    // Create chunk if it doesn't exist (using chunk pool!)
                    ChunkCoord coord{chunkX, chunkY, chunkZ};
                    if (m_chunkMap.find(coord) == m_chunkMap.end()) {
                        auto chunk = acquireChunk(chunkX, chunkY, chunkZ);
                        Chunk* chunkPtr = chunk.get();
                        m_chunkMap[coord] = std::move(chunk);
                        m_chunks.push_back(chunkPtr);
                    }

                    Chunk* chunkPtr = m_chunkMap[coord].get();

                    // Split by surface/underground for parallel generation
                    if (chunkY >= 0) {
                        surfaceChunks.push_back(chunkPtr);
                    } else {
                        undergroundChunks.push_back(chunkPtr);
                    }

                    // Track inner chunks for decoration
                    bool isInner = std::abs(dx) <= radius && std::abs(dy) <= radius && std::abs(dz) <= radius;
                    if (isInner) {
                        innerChunks.push_back(chunkPtr);
                    }
                }
            }
        }
    }  // Lock released here before terrain generation

    size_t totalChunks = surfaceChunks.size() + undergroundChunks.size();
    Logger::info() << "Generating terrain: " << surfaceChunks.size() << " surface + "
                   << undergroundChunks.size() << " underground = " << totalChunks << " total";

    // Step 1: Generate terrain in TWO PARALLEL THREADS (surface + underground)
    BiomeMap* biomeMapPtr = m_biomeMap.get();

    auto generateChunks = [biomeMapPtr](std::vector<Chunk*>& chunks) {
        // Use all available cores for this batch
        unsigned int numThreads = std::max(1u, std::thread::hardware_concurrency() / 2);
        const size_t chunksPerThread = (chunks.size() + numThreads - 1) / numThreads;

        std::vector<std::thread> threads;
        threads.reserve(numThreads);

        for (unsigned int i = 0; i < numThreads; ++i) {
            size_t startIdx = i * chunksPerThread;
            size_t endIdx = std::min(startIdx + chunksPerThread, chunks.size());

            if (startIdx >= chunks.size()) break;

            threads.emplace_back([&chunks, biomeMapPtr, startIdx, endIdx]() {
                for (size_t j = startIdx; j < endIdx; ++j) {
                    chunks[j]->generate(biomeMapPtr);
                }
            });
        }

        for (auto& thread : threads) {
            thread.join();
        }
    };

    // Launch surface and underground generation in parallel
    std::thread surfaceThread([&]() {
        Logger::info() << "Surface thread: generating " << surfaceChunks.size() << " chunks...";
        generateChunks(surfaceChunks);
        Logger::info() << "Surface thread: complete";
    });

    std::thread undergroundThread([&]() {
        Logger::info() << "Underground thread: generating " << undergroundChunks.size() << " chunks...";
        generateChunks(undergroundChunks);
        Logger::info() << "Underground thread: complete";
    });

    surfaceThread.join();
    undergroundThread.join();

    // Step 2: Decorate ONLY inner chunks (spawn area)
    // Outer chunks are terrain-only buffer for streaming
    Logger::info() << "Decorating " << innerChunks.size() << " inner spawn chunks with trees...";

    for (Chunk* chunk : innerChunks) {
        if (chunk->getChunkY() >= 0) {  // Only decorate surface chunks
            decorateChunk(chunk);
            chunk->setNeedsDecoration(false);  // Mark as decorated
        }
    }

    // Step 3: Initialize lighting for INNER chunks only
    // Outer chunks get lighting when they become visible
    Logger::info() << "Initializing lighting for " << innerChunks.size() << " inner spawn chunks...";

    for (Chunk* chunk : innerChunks) {
        initializeChunkLighting(chunk);
        chunk->markLightingDirty();  // Let lighting system propagate
    }

    // Mark outer chunks as terrain-only (no decoration needed)
    for (Chunk* chunk : surfaceChunks) {
        bool isOuter = true;
        for (Chunk* inner : innerChunks) {
            if (chunk == inner) {
                isOuter = false;
                break;
            }
        }
        if (isOuter) {
            chunk->setNeedsDecoration(false);  // Skip decoration for outer chunks
        }
    }

    Logger::info() << "Spawn generation complete: " << innerChunks.size() << " decorated, "
                   << (totalChunks - innerChunks.size()) << " terrain-only buffer";
}

void World::generateWorld() {
    Logger::warning() << "generateWorld() called - this is slow for large worlds!";
    Logger::warning() << "Consider using generateSpawnChunks() + WorldStreaming for better performance";

    if (m_chunks.empty()) {
        Logger::info() << "Creating all chunks for world bounds (test mode)";
        std::unique_lock<std::shared_mutex> lock(m_chunkMapMutex);

        int halfWidth = m_width / 2;
        int halfHeight = m_height / 2;
        int halfDepth = m_depth / 2;

        for (int x = -halfWidth; x < m_width - halfWidth; x++) {
            for (int y = -halfHeight; y < m_height - halfHeight; y++) {
                for (int z = -halfDepth; z < m_depth - halfDepth; z++) {
                    auto chunk = acquireChunk(x, y, z);
                    Chunk* chunkPtr = chunk.get();
                    m_chunkMap[{x, y, z}] = std::move(chunk);
                    m_chunks.push_back(chunkPtr);
                }
            }
        }
        Logger::info() << "Created " << m_chunks.size() << " chunks";
    }

    // Parallel chunk generation for better performance
    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) {
        // hardware_concurrency() can return 0 in containers/CI environments
        numThreads = std::min<unsigned int>(static_cast<unsigned int>(m_chunks.size()), 4);
        Logger::warning() << "hardware_concurrency() returned 0, using fallback: " << numThreads << " threads";
    }
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

    // Track modified chunks for selective mesh regeneration
    std::unordered_set<Chunk*> modifiedChunks;

    // Get all surface chunks (Y > 0) that exist in the world
    std::vector<Chunk*> surfaceChunks;
    {
        std::shared_lock<std::shared_mutex> lock(m_chunkMapMutex);
        for (const auto& pair : m_chunkMap) {
            Chunk* chunk = pair.second.get();
            if (chunk->getChunkY() >= 0) {  // Surface chunks only
                surfaceChunks.push_back(chunk);
            }
        }
    }

    // Decorate surface - grid-based tree sampling for proper density
    // Sample every 4 blocks on a grid (8x8 = 64 sample points per chunk)
    const int TREE_SAMPLE_SPACING = 4;

    for (Chunk* chunk : surfaceChunks) {
        int chunkX = chunk->getChunkX();
        int chunkZ = chunk->getChunkZ();

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
    }  // End surface chunk loop

    // Get all underground chunks for underground decoration
    std::vector<Chunk*> undergroundChunks;
    {
        std::shared_lock<std::shared_mutex> lock(m_chunkMapMutex);
        for (const auto& pair : m_chunkMap) {
            Chunk* chunk = pair.second.get();
            if (chunk->getChunkY() < 0) {  // Underground chunks only
                undergroundChunks.push_back(chunk);
            }
        }
    }

    // Decorate underground biome chambers
    for (Chunk* chunk : undergroundChunks) {
        int chunkX = chunk->getChunkX();
        int chunkY = chunk->getChunkY();
        int chunkZ = chunk->getChunkZ();

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

bool World::hasHorizontalNeighbors(Chunk* chunk) {
    if (!chunk) return false;

    int chunkX = chunk->getChunkX();
    int chunkY = chunk->getChunkY();
    int chunkZ = chunk->getChunkZ();

    // Check all 4 horizontal neighbors (same Y level)
    Chunk* neighborNorth = getChunkAt(chunkX, chunkY, chunkZ + 1);  // +Z
    Chunk* neighborSouth = getChunkAt(chunkX, chunkY, chunkZ - 1);  // -Z
    Chunk* neighborEast = getChunkAt(chunkX + 1, chunkY, chunkZ);   // +X
    Chunk* neighborWest = getChunkAt(chunkX - 1, chunkY, chunkZ);   // -X

    // MULTI-STAGE GENERATION + EDGE CHUNK FIX (2025-11-24):
    // Allow decoration if:
    //   1. Neighbor doesn't exist (nullptr) → Edge chunk, neighbor outside load radius → OK to decorate
    //   2. Neighbor exists → Must be terrain-ready before we decorate
    //
    // This solves two problems:
    //   - Internal chunks: Wait for neighbors to be terrain-ready → No cut-off decorations
    //   - Edge chunks: Can decorate even with missing neighbors → No infinite wait (169 chunks stuck!)
    //
    // Trade-off: Edge chunks might have cut-off decorations at world boundary, but:
    //   - Only visible at extreme edge of loaded world
    //   - Temporary (as player moves, more chunks load and decorate)
    //   - Much better than 169 chunks stuck causing massive performance degradation
    bool northOk = (neighborNorth == nullptr) || neighborNorth->isTerrainReady();
    bool southOk = (neighborSouth == nullptr) || neighborSouth->isTerrainReady();
    bool eastOk = (neighborEast == nullptr) || neighborEast->isTerrainReady();
    bool westOk = (neighborWest == nullptr) || neighborWest->isTerrainReady();

    // DIAGNOSTIC (2025-11-24): Debug the deadlock - sample one chunk periodically
    static int debugCounter = 0;
    static int lastChunkX = -999, lastChunkY = -999, lastChunkZ = -999;
    if (++debugCounter % 600 == 0 && (chunkX != lastChunkX || chunkY != lastChunkY || chunkZ != lastChunkZ)) {
        lastChunkX = chunkX; lastChunkY = chunkY; lastChunkZ = chunkZ;
        Logger::info() << "[NEIGHBOR DEBUG] Chunk(" << chunkX << "," << chunkY << "," << chunkZ << ")"
                      << " | N:" << (neighborNorth ? (neighborNorth->isTerrainReady() ? "READY" : "NOT_READY") : "NULL")
                      << " | S:" << (neighborSouth ? (neighborSouth->isTerrainReady() ? "READY" : "NOT_READY") : "NULL")
                      << " | E:" << (neighborEast ? (neighborEast->isTerrainReady() ? "READY" : "NOT_READY") : "NULL")
                      << " | W:" << (neighborWest ? (neighborWest->isTerrainReady() ? "READY" : "NOT_READY") : "NULL");
    }

    return northOk && southOk && eastOk && westOk;
}

void World::processPendingDecorations(VulkanRenderer* renderer, WorldStreaming* streaming, int maxChunks) {
    // ============================================================================
    // ASYNC DECORATION PIPELINE (2025-11-24): NEVER block main thread!
    // ============================================================================
    // OLD: Launch decoration → WAIT → lighting → mesh → WAIT → upload (2.4s frames!)
    // NEW: Check completed decorations → lighting+mesh+upload, Launch new decorations → return immediately
    // ============================================================================

    // PHASE 1: Process decorations that completed in background (from previous frames)
    // PERFORMANCE FIX (2025-11-25): Mesh generation now uses async worker threads!
    // Only lighting init runs on main thread (fast - just scans for emissive blocks)
    // Can process many completed decorations per frame since mesh gen is queued async
    const int MAX_COMPLETED_PER_FRAME = 10;  // Process up to 10 completed chunks per frame

    std::vector<Chunk*> completedChunks;
    {
        std::lock_guard<std::mutex> lock(m_decorationsInProgressMutex);
        auto it = m_decorationsInProgress.begin();
        while (it != m_decorationsInProgress.end() && completedChunks.size() < MAX_COMPLETED_PER_FRAME) {
            // Check if decoration finished (non-blocking check!)
            if (it->future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                completedChunks.push_back(it->chunk);
                it = m_decorationsInProgress.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Process completed decorations: queue for async mesh + lighting
    // PERFORMANCE FIX (2025-11-25): Both lighting and mesh now run on worker threads!
    if (!completedChunks.empty()) {
        // ASYNC PATH: Queue to mesh worker threads (lighting happens there too)
        // This eliminates ALL main thread stalls from decorated chunk processing
        if (streaming && streaming->isActive()) {
            for (Chunk* chunk : completedChunks) {
                if (!chunk->needsDecoration()) {  // Only if decoration succeeded
                    streaming->queueChunkForMeshing(chunk->getChunkX(), chunk->getChunkY(), chunk->getChunkZ());
                }
            }
            Logger::info() << "Queued " << completedChunks.size() << " decorated chunks for async lighting+meshing";
        } else if (renderer) {
            // Fallback: sync path if no streaming system available
            Logger::warning() << "No streaming system - using sync lighting+mesh (may cause stalls)";
            for (Chunk* chunk : completedChunks) {
                if (!chunk->needsDecoration()) {
                    try {
                        initializeChunkLighting(chunk);
                        chunk->generateMesh(this);
                    } catch (const std::exception& e) {
                        Logger::error() << "Failed to process decorated chunk: " << e.what();
                    }
                }
            }

            // Batched GPU upload (only for sync path)
            try {
                renderer->beginBatchedChunkUploads();
                for (Chunk* chunk : completedChunks) {
                    if (!chunk->needsDecoration()) {
                        renderer->addChunkToBatch(chunk);
                    }
                }
                renderer->submitBatchedChunkUploads();
            } catch (const std::exception& e) {
                Logger::error() << "Failed to batch upload decorated chunks: " << e.what();
            }
        }

        // Remove from pending decorations
        {
            std::lock_guard<std::mutex> lock(m_pendingDecorationsMutex);
            for (Chunk* chunk : completedChunks) {
                m_pendingDecorations.erase(chunk);
                m_pendingDecorationTimestamps.erase(chunk);  // Clean up timestamp
            }
        }

        size_t pendingCount;
        {
            std::lock_guard<std::mutex> lock(m_pendingDecorationsMutex);
            pendingCount = m_pendingDecorations.size();
        }
        Logger::info() << "Processed " << completedChunks.size()
                      << " decorated chunks (" << pendingCount << " still pending)";
    }

    // PHASE 2: Launch NEW decoration tasks (don't block!)
    // Check how many decoration slots are available
    int currentDecorations = 0;
    {
        std::lock_guard<std::mutex> lock(m_decorationsInProgressMutex);
        currentDecorations = m_decorationsInProgress.size();
    }

    const int MAX_CONCURRENT_DECORATIONS = 8;  // INCREASED: 4 → 8 for faster throughput
    int slotsAvailable = MAX_CONCURRENT_DECORATIONS - currentDecorations;

    if (slotsAvailable <= 0) {
        return;  // All slots full, try again next frame
    }

    // Check if there's any work to do
    size_t pendingCount = 0;
    {
        std::lock_guard<std::mutex> lock(m_pendingDecorationsMutex);
        pendingCount = m_pendingDecorations.size();
        if (m_pendingDecorations.empty()) return;
    }

    // Collect chunks ready for decoration (limit to available slots)
    std::vector<Chunk*> chunksToDecorate;
    int blockedByNeighbors = 0;
    int notNeedingDecoration = 0;
    int skippedByTimeout = 0;  // CHANGED: Don't force, just skip

    auto now = std::chrono::steady_clock::now();
    const auto TIMEOUT_THRESHOLD = std::chrono::milliseconds(500);  // REDUCED: 3s → 500ms aggressive timeout

    {
        // THREAD SAFETY: Lock while iterating pending decorations
        std::lock_guard<std::mutex> lock(m_pendingDecorationsMutex);
        auto it = m_pendingDecorations.begin();

        // Only collect as many as we have slots for
        while (it != m_pendingDecorations.end() && chunksToDecorate.size() < slotsAvailable) {
            Chunk* chunk = *it;

            if (!chunk) {
                ++it;
                continue;
            }

            if (!chunk->needsDecoration()) {
                notNeedingDecoration++;
                ++it;
                continue;
            }

            // PERFORMANCE FIX (2025-11-24): Skip decorations that timeout waiting for neighbors
            // DON'T force them - this causes massive stalls when decorations cross chunk boundaries
            // Better to have some chunks without decorations than multi-second frame stalls
            bool skipDueToTimeout = false;
            auto timestampIt = m_pendingDecorationTimestamps.find(chunk);
            if (timestampIt != m_pendingDecorationTimestamps.end()) {
                auto waitTime = now - timestampIt->second;
                if (waitTime > TIMEOUT_THRESHOLD) {
                    skipDueToTimeout = true;
                    skippedByTimeout++;

                    // Remove from pending - we're giving up on this chunk
                    m_pendingDecorations.erase(it++);
                    m_pendingDecorationTimestamps.erase(chunk);
                    chunk->setNeedsDecoration(false);  // Mark as "decorated" (even though we skipped)
                    continue;
                }
            }

            // Check if neighbors are ready
            if (!hasHorizontalNeighbors(chunk)) {
                blockedByNeighbors++;
                ++it;
                continue;
            }

            chunksToDecorate.push_back(chunk);
            ++it;
        }
    }

    // DIAGNOSTIC OUTPUT (2025-11-24): Debug decoration system
    static int diagnosticCounter = 0;
    if (++diagnosticCounter % 300 == 0 && pendingCount > 0) {  // Every 5 seconds at 60 FPS
        Logger::info() << "[DECORATION DIAGNOSTIC] Pending: " << pendingCount
                      << " | Slots: " << slotsAvailable
                      << " | Blocked by neighbors: " << blockedByNeighbors
                      << " | Already decorated: " << notNeedingDecoration
                      << " | Skipped by timeout: " << skippedByTimeout
                      << " | Ready to process: " << chunksToDecorate.size();
    }

    // Log when skipping chunks due to timeout (performance optimization)
    if (skippedByTimeout > 0) {
        Logger::info() << "[DECORATION SKIP] Skipped " << skippedByTimeout
                      << " chunks after 500ms timeout (neighbors not ready - preventing stalls)";
    }

    // Launch decoration tasks (DON'T WAIT!)
    if (!chunksToDecorate.empty()) {
        std::lock_guard<std::mutex> lock(m_decorationsInProgressMutex);

        for (Chunk* chunk : chunksToDecorate) {
            // Launch async decoration task (returns immediately!)
            auto future = std::async(std::launch::async, [this, chunk]() {
                try {
                    decorateChunk(chunk);
                    chunk->setNeedsDecoration(false);
                } catch (const std::exception& e) {
                    Logger::error() << "Failed to decorate chunk (" << chunk->getChunkX() << ", "
                                   << chunk->getChunkY() << ", " << chunk->getChunkZ() << "): " << e.what();
                }
            });

            // Store future in in-progress list (DON'T WAIT!)
            DecorationTask task;
            task.chunk = chunk;
            task.future = std::move(future);
            task.startTime = std::chrono::steady_clock::now();
            m_decorationsInProgress.push_back(std::move(task));
        }
    }

    // That's it! Return immediately - decorations run in background
    // Next frame will pick up completed work in PHASE 1
}

void World::decorateChunk(Chunk* chunk) {
    using namespace TerrainGeneration;

    if (!chunk || chunk->getChunkY() < 0) {
        return;  // Only decorate surface chunks
    }

    // DETERMINISTIC SEEDING: Use chunk coordinates + world seed
    // This ensures the same chunk always gets the same trees, regardless of load order
    int chunkX = chunk->getChunkX();
    int chunkY = chunk->getChunkY();
    int chunkZ = chunk->getChunkZ();

    // Hash chunk coords into seed (same chunk coords always give same seed)
    uint64_t chunkSeed = m_seed + 77777;  // Decoration offset
    chunkSeed ^= (uint64_t)chunkX * 73856093;
    chunkSeed ^= (uint64_t)chunkY * 19349663;
    chunkSeed ^= (uint64_t)chunkZ * 83492791;

    std::mt19937 rng(chunkSeed);
    std::uniform_int_distribution<int> densityDist(0, 100);

    // Grid-based tree sampling (same as decorateWorld)
    const int TREE_SAMPLE_SPACING = 4;

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

            // Get terrain height from biome map
            int terrainHeight = m_biomeMap->getTerrainHeightAt(worldX, worldZ);

            // PERFORMANCE FIX (2025-11-24): Use direct chunk access instead of getBlockAt()
            // getBlockAt() locks mutex every time - 320+ locks per decoration!
            // Direct chunk access = zero locks (chunk already guaranteed to exist)
            int groundY = terrainHeight;

            // Check if position is within THIS chunk (common case - fast path)
            bool inCurrentChunk = (static_cast<int>(worldX) >= chunkX * Chunk::WIDTH &&
                                   static_cast<int>(worldX) < (chunkX + 1) * Chunk::WIDTH &&
                                   static_cast<int>(worldZ) >= chunkZ * Chunk::DEPTH &&
                                   static_cast<int>(worldZ) < (chunkZ + 1) * Chunk::DEPTH);

            if (inCurrentChunk) {
                // Fast path: Direct chunk access (no mutex!)
                int localX = static_cast<int>(worldX) - chunkX * Chunk::WIDTH;
                int localZ = static_cast<int>(worldZ) - chunkZ * Chunk::DEPTH;

                for (int y = terrainHeight; y >= std::max(0, terrainHeight - 5); y--) {
                    int localY = y - chunkY * Chunk::HEIGHT;
                    if (localY >= 0 && localY < Chunk::HEIGHT) {
                        int blockID = chunk->getBlock(localX, localY, localZ);
                        if (blockID != BLOCK_AIR && blockID != BLOCK_WATER) {
                            groundY = y;
                            break;
                        }
                    }
                }
            } else {
                // Slow path: Need to access neighbor chunk (rare - happens at chunk edges)
                for (int y = terrainHeight; y >= std::max(0, terrainHeight - 5); y--) {
                    int blockID = getBlockAt(worldX, static_cast<float>(y), worldZ);
                    if (blockID != BLOCK_AIR && blockID != BLOCK_WATER) {
                        groundY = y;
                        break;
                    }
                }
            }

            if (groundY < 10) continue;  // Too low for tree placement

            // Bounds check before casting to int
            if (worldX < INT_MIN || worldX > INT_MAX || worldZ < INT_MIN || worldZ > INT_MAX) {
                continue;
            }
            int blockX = static_cast<int>(worldX);
            int blockZ = static_cast<int>(worldZ);

            // Place tree (no mesh regeneration - will be done after chunk is uploaded)
            m_treeGenerator->placeTree(this, blockX, groundY + 1, blockZ, biome);
        }
    }
}

void World::initializeChunkLighting(Chunk* chunk) {
    if (!chunk || !m_lightingSystem) return;

    // ========== OPTIMIZATION (2025-11-25): SKIP SCAN IF NO EMISSIVE BLOCKS EXIST ==========
    //
    // OLD: Scanned all 32,768 blocks checking each for emissive properties
    // NEW: Check cached emissive block list first - if empty, skip entire scan!
    //
    // Currently no emissive blocks (torch, lava, glowstone) are defined in YAML,
    // so this skips the entire 32K block scan for EVERY chunk = massive speedup!
    // ==================================================================================

    const auto& emissiveBlockIDs = BlockRegistry::instance().getEmissiveBlockIDs();

    // FAST PATH: No emissive blocks defined - skip entire scan!
    if (emissiveBlockIDs.empty()) {
        chunk->initializeInterpolatedLighting();
        return;
    }

    // SLOW PATH: Emissive blocks exist - scan only for those specific block IDs
    // Build a fast lookup set for O(1) checking
    std::unordered_set<int> emissiveSet(emissiveBlockIDs.begin(), emissiveBlockIDs.end());

    int foundCount = 0;
    for (int x = 0; x < Chunk::WIDTH; x++) {
        for (int y = 0; y < Chunk::HEIGHT; y++) {
            for (int z = 0; z < Chunk::DEPTH; z++) {
                int blockID = chunk->getBlock(x, y, z);

                // Only check if this block ID is in our emissive set
                if (emissiveSet.count(blockID)) {
                    try {
                        const auto& blockDef = BlockRegistry::instance().get(blockID);
                        // Found emissive block - add as block light source
                        int worldX = (chunk->getChunkX() << 5) + x;
                        int worldY = (chunk->getChunkY() << 5) + y;
                        int worldZ = (chunk->getChunkZ() << 5) + z;

                        m_lightingSystem->addLightSource(
                            glm::vec3(worldX, worldY, worldZ),
                            blockDef.lightLevel
                        );
                        foundCount++;
                    } catch (...) {
                        // Unknown block, skip
                    }
                }
            }
        }
    }

    if (foundCount > 0) {
        Logger::debug() << "Chunk (" << chunk->getChunkX() << ", " << chunk->getChunkY()
                       << ", " << chunk->getChunkZ() << ") has " << foundCount << " light sources";
    }

    // Initialize interpolated lighting values to match target values
    // This prevents fade-in effect on newly loaded chunks
    chunk->initializeInterpolatedLighting();
}

void World::updateInterpolatedLighting(float deltaTime) {
    // Update interpolated lighting for all loaded chunks
    // This creates smooth, natural lighting transitions over time
    std::shared_lock<std::shared_mutex> lock(m_chunkMapMutex);

    for (auto& chunk : m_chunks) {
        if (chunk) {
            chunk->updateInterpolatedLighting(deltaTime);
        }
    }
}

void World::registerWaterInChunk(Chunk* chunk) {
    if (!chunk) return;

    auto& registry = BlockRegistry::instance();
    int chunkX = chunk->getChunkX();
    int chunkY = chunk->getChunkY();
    int chunkZ = chunk->getChunkZ();

    // Scan all blocks in chunk
    for (int localX = 0; localX < Chunk::WIDTH; localX++) {
        for (int localY = 0; localY < Chunk::HEIGHT; localY++) {
            for (int localZ = 0; localZ < Chunk::DEPTH; localZ++) {
                int blockID = chunk->getBlock(localX, localY, localZ);

                // Check if this is a liquid block
                if (blockID > 0 && blockID < registry.count() && registry.get(blockID).isLiquid) {
                    // Calculate world coordinates
                    int worldX = chunkX * Chunk::WIDTH + localX;
                    int worldY = chunkY * Chunk::HEIGHT + localY;
                    int worldZ = chunkZ * Chunk::DEPTH + localZ;

                    // Register with simulation system
                    m_waterSimulation->setWaterLevel(worldX, worldY, worldZ, 255, 1);

                    // FIXED (2025-11-23): Mark naturally generated water as sources so they flow
                    // Water blocks with metadata=0 are source blocks (oceans, lakes, placed water)
                    // This makes them maintain their level and flow continuously like Minecraft
                    uint8_t metadata = chunk->getBlockMetadata(localX, localY, localZ);
                    if (metadata == 0) {
                        glm::ivec3 waterPos(worldX, worldY, worldZ);
                        m_waterSimulation->addWaterSource(waterPos, 1);
                    }
                }
            }
        }
    }
}

void World::registerWaterBlocks() {
    Logger::info() << "Registering water blocks with simulation system...";

    int waterBlocksFound = 0;
    int waterSourcesCreated = 0;

    std::shared_lock<std::shared_mutex> lock(m_chunkMapMutex);

    // Register water in all existing chunks
    for (auto& chunk : m_chunks) {
        size_t beforeSize = m_waterSimulation->getActiveWaterChunks().size();
        registerWaterInChunk(chunk);
        size_t afterSize = m_waterSimulation->getActiveWaterChunks().size();

        // Approximate count (not exact since multiple chunks can add to same water chunk)
        waterBlocksFound += (afterSize - beforeSize) * 64;  // Rough estimate
    }

    Logger::info() << "Registered water blocks in " << m_chunks.size() << " chunks with simulation";
}

void World::createBuffers(VulkanRenderer* renderer) {
    std::shared_lock<std::shared_mutex> lock(m_chunkMapMutex);

    renderer->beginBufferCopyBatch();

    for (auto& chunk : m_chunks) {
        if (chunk->getVertexCount() > 0 || chunk->getTransparentVertexCount() > 0) {
            chunk->createVertexBufferBatched(renderer);
        }
    }

    renderer->submitBufferCopyBatch();

    for (auto& chunk : m_chunks) {
        if (chunk->getVertexCount() > 0 || chunk->getTransparentVertexCount() > 0) {
            chunk->cleanupStagingBuffers(renderer);
        }
    }
}

void World::cleanup(VulkanRenderer* renderer) {
    std::shared_lock<std::shared_mutex> lock(m_chunkMapMutex);

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
    // Chunks are 32x32x32 blocks = 32x32x32 world units
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

    // THREAD SAFETY: Acquire shared lock for reading m_chunks
    // Prevents iterator invalidation while other threads modify the chunk list
    std::shared_lock<std::shared_mutex> lock(m_chunkMapMutex);

    // ========== PASS 1: RENDER OPAQUE GEOMETRY ==========
#if USE_INDIRECT_DRAWING
    // GPU OPTIMIZATION: Build indirect draw commands for all visible chunks
    std::vector<VkDrawIndexedIndirectCommand> opaqueDrawCommands;
    opaqueDrawCommands.reserve(m_chunks.size());  // Preallocate for performance

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
        glm::vec3 chunkMin = chunk->getMin();
        glm::vec3 chunkMax = chunk->getMax();

        if (!frustumAABBIntersect(frustum, chunkMin, chunkMax, frustumMargin)) {
            frustumCulled++;
            continue;
        }

        // Chunk passed culling - add to indirect draw command buffer
        VkDrawIndexedIndirectCommand cmd{};
        cmd.indexCount = chunk->getIndexCount();
        cmd.instanceCount = 1;
        cmd.firstIndex = static_cast<uint32_t>(chunk->getMegaBufferIndexOffset() / sizeof(uint32_t));
        cmd.vertexOffset = static_cast<int32_t>(chunk->getMegaBufferBaseVertex());
        cmd.firstInstance = 0;
        opaqueDrawCommands.push_back(cmd);

        renderedCount++;

        // If chunk has transparent geometry, add to transparent list
        if (chunk->getTransparentVertexCount() > 0) {
            transparentChunks.push_back(std::make_pair(chunk, distanceSquared));
        }
    }

    // Execute single indirect draw call for all opaque chunks
    if (!opaqueDrawCommands.empty() && renderer != nullptr) {
        // Upload draw commands to indirect buffer
        void* data;
        VkDeviceMemory indirectBufferMemory = renderer->getIndirectDrawBufferMemory();
        VkBuffer indirectBuffer = renderer->getIndirectDrawBuffer();

        vkMapMemory(renderer->getDevice(), indirectBufferMemory, 0,
                   opaqueDrawCommands.size() * sizeof(VkDrawIndexedIndirectCommand), 0, &data);
        memcpy(data, opaqueDrawCommands.data(),
               opaqueDrawCommands.size() * sizeof(VkDrawIndexedIndirectCommand));
        vkUnmapMemory(renderer->getDevice(), indirectBufferMemory);

        // Bind mega-buffers
        VkBuffer vertexBuffers[] = {renderer->getMegaVertexBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(commandBuffer, renderer->getMegaIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

        // SINGLE DRAW CALL for all opaque chunks!
        vkCmdDrawIndexedIndirect(commandBuffer, indirectBuffer, 0,
                                static_cast<uint32_t>(opaqueDrawCommands.size()),
                                sizeof(VkDrawIndexedIndirectCommand));
    }

#else
    // LEGACY PATH: Per-chunk draw calls
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
#endif

    // ========== PASS 2: RENDER TRANSPARENT GEOMETRY (SORTED) ==========
    if (!transparentChunks.empty() && renderer != nullptr) {
        // Bind transparent pipeline (depth test enabled, depth write disabled)
        renderer->bindPipelineCached(commandBuffer, renderer->getTransparentPipeline());

        // IMPORTANT: Rebind descriptor sets (contains texture atlas)
        VkDescriptorSet descriptorSet = renderer->getCurrentDescriptorSet();
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                               renderer->getPipelineLayout(), 0, 1,
                               &descriptorSet, 0, nullptr);

        // Sort transparent chunks back-to-front (farthest first)
        // OPTIMIZATION: Only re-sort if camera moved significantly (saves O(n log n) every frame)
        if (glm::distance(m_lastSortPosition, cameraPos) > 5.0f) {
            std::sort(transparentChunks.begin(), transparentChunks.end(),
                      [](const auto& a, const auto& b) {
                          return a.second > b.second;  // Greater distance first
                      });
            m_lastSortPosition = cameraPos;
        }

#if USE_INDIRECT_DRAWING
        // Build indirect draw commands for transparent geometry
        std::vector<VkDrawIndexedIndirectCommand> transparentDrawCommands;
        transparentDrawCommands.reserve(transparentChunks.size());

        for (const auto& pair : transparentChunks) {
            Chunk* chunk = pair.first;
            VkDrawIndexedIndirectCommand cmd{};
            cmd.indexCount = chunk->getTransparentIndexCount();
            cmd.instanceCount = 1;
            cmd.firstIndex = static_cast<uint32_t>(chunk->getMegaBufferTransparentIndexOffset() / sizeof(uint32_t));
            cmd.vertexOffset = static_cast<int32_t>(chunk->getMegaBufferTransparentBaseVertex());
            cmd.firstInstance = 0;
            transparentDrawCommands.push_back(cmd);
        }

        if (!transparentDrawCommands.empty()) {
            // Upload transparent draw commands
            void* data;
            VkDeviceMemory transparentIndirectBufferMemory = renderer->getIndirectDrawTransparentBufferMemory();
            VkBuffer transparentIndirectBuffer = renderer->getIndirectDrawTransparentBuffer();

            vkMapMemory(renderer->getDevice(), transparentIndirectBufferMemory, 0,
                       transparentDrawCommands.size() * sizeof(VkDrawIndexedIndirectCommand), 0, &data);
            memcpy(data, transparentDrawCommands.data(),
                   transparentDrawCommands.size() * sizeof(VkDrawIndexedIndirectCommand));
            vkUnmapMemory(renderer->getDevice(), transparentIndirectBufferMemory);

            // Bind transparent mega-buffers
            VkBuffer vertexBuffers[] = {renderer->getMegaTransparentVertexBuffer()};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(commandBuffer, renderer->getMegaTransparentIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

            // SINGLE DRAW CALL for all transparent chunks!
            vkCmdDrawIndexedIndirect(commandBuffer, transparentIndirectBuffer, 0,
                                    static_cast<uint32_t>(transparentDrawCommands.size()),
                                    sizeof(VkDrawIndexedIndirectCommand));
        }
#else
        // Legacy: Render transparent chunks individually
        for (const auto& pair : transparentChunks) {
            pair.first->render(commandBuffer, true);  // true = transparent
        }
#endif
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
    // ============================================================================
    // WARNING: UNSAFE - NO LOCKING!
    // Caller MUST hold m_chunkMapMutex (shared or unique lock) before calling!
    // Calling this without holding the lock will cause race conditions and crashes!
    // ============================================================================
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

std::vector<ChunkCoord> World::getAllChunkCoords() const {
    // THREAD SAFETY: Shared lock for concurrent reads
    std::shared_lock<std::shared_mutex> lock(m_chunkMapMutex);

    std::vector<ChunkCoord> coords;
    coords.reserve(m_chunkMap.size());

    for (const auto& pair : m_chunkMap) {
        coords.push_back(pair.first);
    }

    return coords;
}

void World::forEachChunkCoord(const std::function<void(const ChunkCoord&)>& callback) const {
    // THREAD SAFETY: Shared lock for concurrent reads
    // Zero-copy iteration - avoids allocating vector of 432 coords
    std::shared_lock<std::shared_mutex> lock(m_chunkMapMutex);

    for (const auto& pair : m_chunkMap) {
        callback(pair.first);
    }
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
    // ============================================================================
    // WARNING: UNSAFE - NO LOCKING!
    // Caller MUST hold m_chunkMapMutex (shared or unique lock) before calling!
    // Calling this without holding the lock will cause race conditions and crashes!
    // ============================================================================
    auto coords = worldToBlockCoords(worldX, worldY, worldZ);
    return getChunkAtUnsafe(coords.chunkX, coords.chunkY, coords.chunkZ);
}

Chunk* World::getChunkAtWorldPos(float worldX, float worldY, float worldZ) {
    // Convert world coordinates to chunk coordinates
    auto coords = worldToBlockCoords(worldX, worldY, worldZ);
    return getChunkAt(coords.chunkX, coords.chunkY, coords.chunkZ);
}

bool World::addStreamedChunk(std::unique_ptr<Chunk> chunk, VulkanRenderer* renderer, bool deferGPUUpload, bool deferMeshGeneration, ChunkLOD lod) {
    if (!chunk) {
        return false;  // Null chunk
    }

    int chunkX = chunk->getChunkX();
    int chunkY = chunk->getChunkY();
    int chunkZ = chunk->getChunkZ();

    // Thread-safe insertion
    std::unique_lock<std::shared_mutex> lock(m_chunkMapMutex);

    ChunkCoord coord{chunkX, chunkY, chunkZ};

    // Check for duplicates
    if (m_chunkMap.find(coord) != m_chunkMap.end()) {
        Logger::warning() << "Chunk (" << chunkX << ", " << chunkY << ", " << chunkZ << ") already exists, discarding streamed chunk";
        return false;
    }

    // Add to map
    Chunk* chunkPtr = chunk.get();
    m_chunkMap[coord] = std::move(chunk);
    m_chunks.push_back(chunkPtr);

    lock.unlock();  // Release lock before decoration (can be slow)

    // LOD TIER (2025-11-25): Skip decoration for non-FULL LOD chunks
    // MESH_ONLY: Skip decoration (fog hides trees anyway)
    // TERRAIN_ONLY: Skip decoration AND mesh (beyond render distance)
    const bool skipDecoration = (lod != ChunkLOD::FULL);

    // MAIN THREAD: Decorate, Light, Mesh, Upload - IN THAT ORDER
    // This is safe because:
    // 1. Chunk is now findable via getChunkAt() for tree placement
    // 2. We're on main thread (no race conditions)
    // 3. Lighting happens AFTER all blocks are placed
    // 4. GPU upload happens AFTER final mesh is generated
    // 5. Mesh generation happens AFTER neighbors are loaded (for occlusion culling)
    if (chunkPtr->getChunkY() >= 0 && !skipDecoration) {  // Only decorate surface chunks with FULL LOD
        try {
            Logger::debug() << "Processing surface chunk (" << chunkX << ", " << chunkY << ", " << chunkZ << "): decorate → light → mesh → upload";

            // FIXED (2025-11-23): Only decorate freshly generated chunks, not loaded ones
            // This prevents overwriting player edits when chunks reload from disk/cache
            if (chunkPtr->needsDecoration()) {
                // DECORATION FIX: Only decorate if all horizontal neighbors are loaded
                // This prevents trees from being placed with missing neighbor data
                if (hasHorizontalNeighbors(chunkPtr)) {
                    // Step 1: Add decorations (trees, structures)
                    decorateChunk(chunkPtr);
                    chunkPtr->setNeedsDecoration(false);  // Mark as decorated
                } else {
                    // Neighbors not ready yet - defer decoration until later
                    // DEADLOCK FIX (2025-11-24): Track when chunk was added to queue for timeout-based processing
                    size_t pendingCount;
                    {
                        std::lock_guard<std::mutex> lock(m_pendingDecorationsMutex);
                        if (m_pendingDecorations.find(chunkPtr) == m_pendingDecorations.end()) {
                            m_pendingDecorations.insert(chunkPtr);
                            m_pendingDecorationTimestamps[chunkPtr] = std::chrono::steady_clock::now();
                        }
                        pendingCount = m_pendingDecorations.size();
                    }
                    Logger::debug() << "Chunk (" << chunkX << ", " << chunkY << ", " << chunkZ
                                   << ") waiting for neighbors before decoration (pending: "
                                   << pendingCount << ")";
                }
            } else {
                Logger::debug() << "Chunk (" << chunkX << ", " << chunkY << ", " << chunkZ
                               << ") loaded from disk/cache - skipping decoration to preserve player edits";
            }

            // Step 2: Initialize lighting ONCE after all blocks are in place
            // PERFORMANCE FIX (2025-11-25): Skip if deferred - mesh workers handle lighting async!
            // Only do sync lighting for immediate (non-deferred) mesh generation
            if (!deferMeshGeneration && !chunkPtr->hasLightingData()) {
                initializeChunkLighting(chunkPtr);
            }

            // CRITICAL FIX (2025-11-23): Don't mark fresh chunks dirty!
            // markLightingDirty() causes lighting system to regenerate mesh next frame
            // Fresh chunks don't need re-meshing - we generate the mesh below
            // This eliminates double mesh generation: once here, once in lighting system
            // chunkPtr->markLightingDirty();  // ← REMOVED - prevents duplicate mesh gen

            // Step 3: Generate final mesh with correct lighting (FIRST TIME - worker didn't mesh)
            if (!deferMeshGeneration) {
                chunkPtr->generateMesh(this);
            }

            // Step 4: Upload final mesh to GPU (CRITICAL: after decoration/lighting!)
            if (renderer && !deferGPUUpload && !deferMeshGeneration) {
                Logger::info() << "Uploading surface chunk (" << chunkX << ", " << chunkY << ", " << chunkZ
                               << ") with " << chunkPtr->getVertexCount() << " vertices (lit+decorated)";
                renderer->beginAsyncChunkUpload();
                chunkPtr->createVertexBufferBatched(renderer);
                renderer->submitAsyncChunkUpload(chunkPtr);
            }
        } catch (const std::exception& e) {
            Logger::error() << "Failed to decorate/light chunk (" << chunkX << ", " << chunkY << ", " << chunkZ << "): " << e.what();
            // Continue anyway - chunk has terrain even without decoration
        }
    } else if (!skipDecoration) {
        // Underground chunks: Light and mesh (no decoration)
        Logger::debug() << "Processing underground chunk (" << chunkX << ", " << chunkY << ", " << chunkZ << "): light → mesh → upload";

        // PERFORMANCE FIX (2025-11-25): Skip if deferred - mesh workers handle lighting async!
        if (!deferMeshGeneration && !chunkPtr->hasLightingData()) {
            initializeChunkLighting(chunkPtr);
        }
        // CRITICAL FIX (2025-11-23): Don't mark fresh underground chunks dirty either!
        // chunkPtr->markLightingDirty();  // ← REMOVED - prevents duplicate mesh gen

        if (!deferMeshGeneration) {
            chunkPtr->generateMesh(this);
        }

        // Upload to GPU (async to prevent frame stalls)
        if (renderer && !deferGPUUpload && !deferMeshGeneration) {
            Logger::info() << "Uploading underground chunk (" << chunkX << ", " << chunkY << ", " << chunkZ
                           << ") with " << chunkPtr->getVertexCount() << " vertices (lit)";
            renderer->beginAsyncChunkUpload();
            chunkPtr->createVertexBufferBatched(renderer);
            renderer->submitAsyncChunkUpload(chunkPtr);
        }
    } else {
        // LOD SKIP MODE (2025-11-25): Skip decoration for distant chunks
        // MESH_ONLY: Terrain visible in fog, but trees not visible → skip decoration
        // TERRAIN_ONLY: Beyond render distance → skip decoration AND mesh
        Logger::debug() << "LOD skip for chunk (" << chunkX << ", " << chunkY << ", " << chunkZ
                       << ") - LOD=" << static_cast<int>(lod) << " (skipping decoration)";

        // Mark as "decorated" since we're intentionally skipping - don't re-queue
        if (chunkPtr->needsDecoration()) {
            chunkPtr->setNeedsDecoration(false);
        }

        // Initialize basic lighting for visual consistency (even if mesh deferred)
        chunkPtr->initializeInterpolatedLighting();

        // Mesh generation deferred to worker threads for MESH_ONLY
        // TERRAIN_ONLY chunks don't need mesh at all (beyond render distance)
        if (!deferMeshGeneration && lod == ChunkLOD::MESH_ONLY) {
            chunkPtr->generateMesh(this);
        }

        // GPU upload only if mesh was generated and not deferred
        if (renderer && !deferGPUUpload && !deferMeshGeneration && lod == ChunkLOD::MESH_ONLY) {
            renderer->beginAsyncChunkUpload();
            chunkPtr->createVertexBufferBatched(renderer);
            renderer->submitAsyncChunkUpload(chunkPtr);
        }
    }

    // FIXED (2025-11-23): Register water blocks in streamed chunks with simulation
    // This ensures water in dynamically loaded chunks can flow properly
    registerWaterInChunk(chunkPtr);

    Logger::debug() << "Added streamed chunk (" << chunkX << ", " << chunkY << ", " << chunkZ << ") to world";

    return true;
}

bool World::removeChunk(int chunkX, int chunkY, int chunkZ, VulkanRenderer* renderer, bool skipWaterCleanup) {
    // Thread-safe removal
    std::unique_lock<std::shared_mutex> lock(m_chunkMapMutex);

    ChunkCoord coord{chunkX, chunkY, chunkZ};

    // Find chunk
    auto it = m_chunkMap.find(coord);
    if (it == m_chunkMap.end()) {
        return false;  // Chunk doesn't exist
    }

    // Remove from vector (order doesn't matter, so use swap-and-pop for O(1))
    Chunk* chunkPtr = it->second.get();

    // DECORATION FIX: Remove from pending decorations if present
    {
        std::lock_guard<std::mutex> decorLock(m_pendingDecorationsMutex);
        m_pendingDecorations.erase(chunkPtr);
        m_pendingDecorationTimestamps.erase(chunkPtr);  // Clean up timestamp
    }
    auto vecIt = std::find(m_chunks.begin(), m_chunks.end(), chunkPtr);
    if (vecIt != m_chunks.end()) {
        std::swap(*vecIt, m_chunks.back());
        m_chunks.pop_back();
    }

    // CRITICAL: Notify lighting system before destroying chunk
    // This prevents dangling pointers in the lighting dirty chunks set
    if (m_lightingSystem) {
        m_lightingSystem->notifyChunkUnload(chunkPtr);
    }

    // PERFORMANCE FIX (2025-11-23): Notify water simulation to clean up water cells
    // Without this, water cells accumulate infinitely causing frame time increase
    // Skip if batch cleanup already performed (50× faster)
    if (m_waterSimulation && !skipWaterCleanup) {
        m_waterSimulation->notifyChunkUnload(chunkX, chunkY, chunkZ);
    }

    // Destroy Vulkan buffers (can't keep GPU resources in cache)
    if (renderer) {
        chunkPtr->destroyBuffers(renderer);
    }

    // EMPTY CHUNK CULLING: Don't cache empty chunks (they're free to regenerate!)
    // This saves RAM for sky chunks and fully-mined chunks
    if (chunkPtr->isEmpty()) {
        Logger::debug() << "Skipping cache for empty chunk (" << chunkX << ", " << chunkY << ", " << chunkZ
                       << "), returning to pool instead";
        releaseChunk(std::move(it->second));
        m_chunkMap.erase(it);
        {
            std::lock_guard<std::mutex> dirtyLock(m_dirtyChunksMutex);
            m_dirtyChunks.erase(coord);  // Remove from dirty set if present
        }
        return true;
    }

    // Move chunk to cache instead of deleting (RAM cache for fast reload)
    // MEMORY OPTIMIZATION (2025-11-25): Deallocate interpolated lighting before caching
    // Saves 256KB per cached chunk since smooth transitions not needed when unloaded
    it->second->deallocateInterpolatedLighting();
    m_unloadedChunksCache[coord] = std::move(it->second);
    m_chunkMap.erase(it);

    // Check cache size limit and evict oldest chunks if needed
    if (m_unloadedChunksCache.size() > m_maxCachedChunks) {
        // Evict first chunk found (simple eviction, could use LRU later)
        auto evictIt = m_unloadedChunksCache.begin();
        ChunkCoord evictCoord = evictIt->first;

        // DISK I/O FIX: Don't save on eviction - let autosave handle it (every 5 min)
        // This prevents constant disk writes during fast movement
        // Modified chunks will be saved during next autosave or manual save
        // If they're modified again before autosave, that's fine - we'll save the latest version
        {
            std::lock_guard<std::mutex> dirtyLock(m_dirtyChunksMutex);
            if (m_dirtyChunks.count(evictCoord) > 0) {
                Logger::debug() << "Evicted dirty chunk (" << evictCoord.x << ", " << evictCoord.y << ", " << evictCoord.z
                               << ") - will save during next autosave";
                // Keep chunk in dirty set for next autosave
            }
        }

        // Return evicted chunk to pool for reuse (CHUNK POOLING)
        std::unique_ptr<Chunk> evictedChunk = std::move(evictIt->second);
        m_unloadedChunksCache.erase(evictIt);
        releaseChunk(std::move(evictedChunk));
        Logger::debug() << "Evicted cached chunk to stay under limit (" << m_maxCachedChunks << ")";
    }

    Logger::debug() << "Moved chunk (" << chunkX << ", " << chunkY << ", " << chunkZ
                   << ") to cache (total cached: " << m_unloadedChunksCache.size() << ")";

    return true;
}

int World::getBlockAtUnsafe(float worldX, float worldY, float worldZ) {
    // ============================================================================
    // WARNING: UNSAFE - NO LOCKING!
    // Caller MUST hold m_chunkMapMutex (shared or unique lock) before calling!
    // Calling this without holding the lock will cause race conditions and crashes!
    // ============================================================================
    auto coords = worldToBlockCoords(worldX, worldY, worldZ);
    Chunk* chunk = getChunkAtUnsafe(coords.chunkX, coords.chunkY, coords.chunkZ);
    if (chunk == nullptr) {
        return 0; // Air (outside world bounds)
    }
    return chunk->getBlock(coords.localX, coords.localY, coords.localZ);
}

void World::setBlockAtUnsafe(float worldX, float worldY, float worldZ, int blockID) {
    // ============================================================================
    // WARNING: UNSAFE - NO LOCKING!
    // Caller MUST hold m_chunkMapMutex (unique lock) before calling!
    // Calling this without holding the lock will cause race conditions and crashes!
    // ============================================================================
    auto coords = worldToBlockCoords(worldX, worldY, worldZ);
    Chunk* chunk = getChunkAtUnsafe(coords.chunkX, coords.chunkY, coords.chunkZ);
    if (chunk == nullptr) {
        return; // Outside world bounds, do nothing
    }
    chunk->setBlock(coords.localX, coords.localY, coords.localZ, blockID);
}

uint8_t World::getBlockMetadataAtUnsafe(float worldX, float worldY, float worldZ) {
    // ============================================================================
    // WARNING: UNSAFE - NO LOCKING!
    // Caller MUST hold m_chunkMapMutex (shared or unique lock) before calling!
    // Calling this without holding the lock will cause race conditions and crashes!
    // ============================================================================
    auto coords = worldToBlockCoords(worldX, worldY, worldZ);
    Chunk* chunk = getChunkAtUnsafe(coords.chunkX, coords.chunkY, coords.chunkZ);
    if (chunk == nullptr) {
        return 0; // Out of bounds
    }
    return chunk->getBlockMetadata(coords.localX, coords.localY, coords.localZ);
}

void World::setBlockMetadataAtUnsafe(float worldX, float worldY, float worldZ, uint8_t metadata) {
    // ============================================================================
    // WARNING: UNSAFE - NO LOCKING!
    // Caller MUST hold m_chunkMapMutex (unique lock) before calling!
    // Calling this without holding the lock will cause race conditions and crashes!
    // ============================================================================
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

    // Mark chunk as dirty (needs saving during next autosave)
    markChunkDirty(coords.chunkX, coords.chunkY, coords.chunkZ);

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

    // Bounds check before registry access to prevent crash
    if (blockID != 0 && blockID >= 0 && blockID < registry.count() && registry.get(blockID).isLiquid) {
        // Only allow breaking source blocks (level 0)
        uint8_t waterLevel = getBlockMetadataAtUnsafe(worldX, worldY, worldZ);
        if (waterLevel > 0) {
            // This is flowing water, can't break it directly
            return;
        }

        // It's a source block - break it and remove all connected flowing water
        setBlockAtUnsafe(worldX, worldY, worldZ, 0);
        setBlockMetadataAtUnsafe(worldX, worldY, worldZ, 0);

        // Unregister water from simulation
        glm::ivec3 waterPos(static_cast<int>(worldX), static_cast<int>(worldY), static_cast<int>(worldZ));
        m_waterSimulation->setWaterLevel(
            waterPos.x,
            waterPos.y,
            waterPos.z,
            0,  // Zero level removes the water cell
            0   // No fluid type
        );

        // FIX: Remove from water sources list to prevent it from regenerating
        m_waterSimulation->removeWaterSource(waterPos);

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
                // Bounds check before registry access to prevent crash
                if (neighborBlock != 0 && neighborBlock >= 0 && neighborBlock < registry.count() && registry.get(neighborBlock).isLiquid) {
                    uint8_t neighborLevel = getBlockMetadataAtUnsafe(neighborPos.x, neighborPos.y, neighborPos.z);
                    if (neighborLevel > 0) {  // It's flowing water
                        setBlockAtUnsafe(neighborPos.x, neighborPos.y, neighborPos.z, 0);
                        setBlockMetadataAtUnsafe(neighborPos.x, neighborPos.y, neighborPos.z, 0);

                        // Unregister from simulation
                        m_waterSimulation->setWaterLevel(
                            static_cast<int>(neighborPos.x),
                            static_cast<int>(neighborPos.y),
                            static_cast<int>(neighborPos.z),
                            0,  // Zero level removes the water cell
                            0   // No fluid type
                        );

                        toCheck.push_back(neighborPos);
                    }
                }
            }
        }
    } else {
        // Normal block - just break it
        setBlockAtUnsafe(worldX, worldY, worldZ, 0);
    }

    // FIX: Mark chunk as dirty so block changes are saved
    auto coords = worldToBlockCoords(worldX, worldY, worldZ);
    markChunkDirtyUnsafe(coords.chunkX, coords.chunkY, coords.chunkZ);

    // LIGHTING FIX: Store lighting info BEFORE calling lighting system
    // (we'll call lighting methods AFTER releasing the lock to avoid deadlock)
    bool needsLightingUpdate = false;
    bool wasEmissive = false;
    uint8_t lightLevel = 0;
    bool wasOpaque = false;
    glm::ivec3 blockPos(static_cast<int>(worldX), static_cast<int>(worldY), static_cast<int>(worldZ));

    if (blockID > 0 && blockID < registry.count()) {
        const auto& blockDef = registry.get(blockID);
        wasEmissive = blockDef.isEmissive && blockDef.lightLevel > 0;
        lightLevel = blockDef.lightLevel;
        wasOpaque = (blockDef.transparency < 0.5f);  // Opaque if transparency < 50%
        needsLightingUpdate = wasEmissive || wasOpaque;  // Need update if emissive or opaque block removed
    }

    // Update the affected chunk and all adjacent chunks
    // Must regenerate MESH (not just vertex buffer) because face culling needs updating
    Chunk* affectedChunk = getChunkAtWorldPosUnsafe(worldX, worldY, worldZ);
    if (affectedChunk) {
        try {
            // Pass true to indicate we already hold the lock (prevents deadlock)
            affectedChunk->generateMesh(this, true);

            // Upload to GPU (async to prevent frame stalls)
            renderer->beginAsyncChunkUpload();
            affectedChunk->createVertexBufferBatched(renderer);
            renderer->submitAsyncChunkUpload(affectedChunk);
        } catch (const std::exception& e) {
            Logger::error() << "Failed to update chunk after breaking block: " << e.what();
            // Mesh is already generated, just buffer creation failed
            // Chunk will still render with old buffer until next update
        }
    }

    // CRITICAL FIX (2025-11-24): Collect chunks to update, then release lock BEFORE mesh generation
    // Old code did mesh+GPU upload while holding lock → deadlock + GPU stalls blocking world access!
    // New code: collect chunks → unlock → mesh → GPU upload

    // Collect neighbor chunks while holding lock
    std::vector<Chunk*> chunksToUpdate;
    chunksToUpdate.push_back(affectedChunk);

    Chunk* neighbors[6] = {
        getChunkAtWorldPosUnsafe(worldX - 1.0f, worldY, worldZ),  // -X
        getChunkAtWorldPosUnsafe(worldX + 1.0f, worldY, worldZ),  // +X
        getChunkAtWorldPosUnsafe(worldX, worldY - 1.0f, worldZ),  // -Y (below)
        getChunkAtWorldPosUnsafe(worldX, worldY + 1.0f, worldZ),  // +Y (above)
        getChunkAtWorldPosUnsafe(worldX, worldY, worldZ - 1.0f),  // -Z
        getChunkAtWorldPosUnsafe(worldX, worldY, worldZ + 1.0f)   // +Z
    };

    for (int i = 0; i < 6; i++) {
        if (neighbors[i] && neighbors[i] != affectedChunk) {
            // Skip duplicates
            bool alreadyAdded = false;
            for (Chunk* existing : chunksToUpdate) {
                if (existing == neighbors[i]) {
                    alreadyAdded = true;
                    break;
                }
            }
            if (!alreadyAdded) {
                chunksToUpdate.push_back(neighbors[i]);
            }
        }
    }

    // IMPORTANT: Release lock BEFORE mesh generation and GPU upload!
    lock.unlock();

    // Now regenerate meshes WITHOUT holding lock (much safer!)
    for (Chunk* chunk : chunksToUpdate) {
        try {
            chunk->generateMesh(this);  // Mesh generation acquires its own locks as needed

            // Upload to GPU
            renderer->beginAsyncChunkUpload();
            chunk->createVertexBufferBatched(renderer);
            renderer->submitAsyncChunkUpload(chunk);
        } catch (const std::exception& e) {
            Logger::error() << "Failed to update chunk after block break: " << e.what();
        }
    }

    // LIGHTING FIX: Update lighting AFTER releasing lock
    // The lighting system methods call getChunkAtWorldPos() which acquires locks
    if (needsLightingUpdate) {
        // If broken block was emissive (torch, lava), remove its light source
        if (wasEmissive) {
            m_lightingSystem->removeLightSource(glm::vec3(blockPos));
        }

        // If broken block was opaque, light can now pass through (air is transparent)
        if (wasOpaque) {
            bool isOpaque = false;  // Air is transparent
            m_lightingSystem->onBlockChanged(blockPos, wasOpaque, isOpaque);
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
    // Bounds check to prevent crash from invalid block IDs
    auto& registry = BlockRegistry::instance();
    if (blockID <= 0 || blockID >= registry.count()) return;

    // Check if there's already a block here (don't place over existing blocks)
    // Use UNSAFE version - we already hold lock
    int existingBlock = getBlockAtUnsafe(worldX, worldY, worldZ);
    if (existingBlock != 0) return;

    // Place the block
    setBlockAtUnsafe(worldX, worldY, worldZ, blockID);

    // FIX: Mark chunk as dirty so block changes are saved
    auto coords = worldToBlockCoords(worldX, worldY, worldZ);
    markChunkDirtyUnsafe(coords.chunkX, coords.chunkY, coords.chunkZ);

    // If placing water, set metadata to 0 (source block) and register with simulation
    if (registry.get(blockID).isLiquid) {
        setBlockMetadataAtUnsafe(worldX, worldY, worldZ, 0);  // Level 0 = source block

        // Register water block with simulation system so it can flow
        glm::ivec3 waterPos(static_cast<int>(worldX), static_cast<int>(worldY), static_cast<int>(worldZ));
        m_waterSimulation->setWaterLevel(
            waterPos.x,
            waterPos.y,
            waterPos.z,
            255,  // Full water level (source block)
            1     // Fluid type: 1=water, 2=lava
        );

        // FIX: Add as water source so it maintains its level and flows continuously
        m_waterSimulation->addWaterSource(waterPos, 1);
    }

    // LIGHTING FIX: Store lighting info BEFORE calling lighting system
    // (we'll call lighting methods AFTER releasing the lock to avoid deadlock)
    const auto& blockDef = registry.get(blockID);
    glm::ivec3 blockPos(static_cast<int>(worldX), static_cast<int>(worldY), static_cast<int>(worldZ));
    bool isEmissive = blockDef.isEmissive && blockDef.lightLevel > 0;
    uint8_t lightLevel = blockDef.lightLevel;
    bool wasOpaque = false;  // Air (existingBlock == 0) was transparent
    bool isOpaque = (blockDef.transparency < 0.5f);  // Opaque if transparency < 50%
    bool needsOpacityUpdate = (wasOpaque != isOpaque);

    // Collect all chunks that need mesh updates while holding lock
    std::vector<Chunk*> chunksToUpdate;

    Chunk* affectedChunk = getChunkAtWorldPosUnsafe(worldX, worldY, worldZ);
    if (affectedChunk) {
        chunksToUpdate.push_back(affectedChunk);
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

    // Collect unique neighbor chunks
    for (int i = 0; i < 6; i++) {
        if (neighbors[i] && neighbors[i] != affectedChunk) {
            // Skip if already in list (deduplicate)
            bool alreadyInList = false;
            for (Chunk* chunk : chunksToUpdate) {
                if (chunk == neighbors[i]) {
                    alreadyInList = true;
                    break;
                }
            }
            if (!alreadyInList) {
                chunksToUpdate.push_back(neighbors[i]);
            }
        }
    }

    // CRITICAL FIX: Release lock BEFORE mesh generation!
    // generateMesh() calls getBlockAt() on neighbors, which needs locks
    lock.unlock();

    // Now regenerate meshes and upload to GPU WITHOUT holding lock
    for (Chunk* chunk : chunksToUpdate) {
        try {
            chunk->generateMesh(this);  // Acquires locks as needed

            // Upload to GPU (async to prevent frame stalls)
            renderer->beginAsyncChunkUpload();
            chunk->createVertexBufferBatched(renderer);
            renderer->submitAsyncChunkUpload(chunk);
        } catch (const std::exception& e) {
            Logger::error() << "Failed to update chunk after placing block: " << e.what();
            // Continue updating other chunks even if one fails
        }
    }

    // LIGHTING FIX: Update lighting AFTER releasing lock
    // The lighting system methods call getChunkAtWorldPos() which acquires locks
    // If placed block is emissive (torch, lava), add its light source
    if (isEmissive) {
        m_lightingSystem->addLightSource(glm::vec3(blockPos), lightLevel);
    }

    // If placing opaque block where there was air, lighting needs update
    if (needsOpacityUpdate) {
        m_lightingSystem->onBlockChanged(blockPos, wasOpaque, isOpaque);
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
    // Water height rendering: IMPLEMENTED in chunk.cpp:552-558 (uses metadata to adjust height)

    auto& registry = BlockRegistry::instance();
    std::unordered_set<Chunk*> chunksToUpdate;

    // Track water blocks that need updating
    struct WaterBlock {
        float x, y, z;
        uint8_t level;
    };
    std::vector<WaterBlock> waterToAdd;

    // Pass 1: Process all water blocks and schedule flows
    // Performance optimization: Only check chunks that exist
    // Water-containing chunks: TRACKED via WaterSimulation::m_activeChunks
    std::vector<Chunk*> allChunks;
    {
        std::shared_lock<std::shared_mutex> lock(m_chunkMapMutex);
        for (const auto& pair : m_chunkMap) {
            allChunks.push_back(pair.second.get());
        }
    }

    for (Chunk* chunk : allChunks) {
        // Early skip: If chunk has no vertices, it's likely empty
        if (chunk->getVertexCount() == 0 && chunk->getTransparentVertexCount() == 0) continue;

        int chunkX = chunk->getChunkX();
        int chunkY = chunk->getChunkY();
        int chunkZ = chunk->getChunkZ();

        // Iterate through blocks in this chunk (top to bottom for proper flow)
        for (int localX = 0; localX < Chunk::WIDTH; localX += 1) {
            for (int localY = Chunk::HEIGHT - 1; localY >= 0; localY -= 1) {
                for (int localZ = 0; localZ < Chunk::DEPTH; localZ += 1) {
                    int blockID = chunk->getBlock(localX, localY, localZ);
                    if (blockID == 0) continue;

                    // Bounds check before registry access to prevent crash
                    if (blockID < 0 || blockID >= registry.count()) continue;

                    const BlockDefinition& def = registry.get(blockID);
                    if (!def.isLiquid) continue;

                    // Calculate world position
                    float worldX = static_cast<float>(chunkX * Chunk::WIDTH + localX);
                    float worldY = static_cast<float>(chunkY * Chunk::HEIGHT + localY);
                    float worldZ = static_cast<float>(chunkZ * Chunk::DEPTH + localZ);

                    // Get water level
                    uint8_t waterLevel = getBlockMetadataAt(worldX, worldY, worldZ);

                    // VERTICAL FLOW: Water always flows down first
                    float belowY = worldY - 1.0f;
                    int blockBelow = getBlockAt(worldX, belowY, worldZ);

                    if (blockBelow == 0) {
                        // Air below - water flows down as SOURCE block (level 0)
                        waterToAdd.push_back({worldX, belowY, worldZ, 0});
                        chunksToUpdate.insert(getChunkAtWorldPos(worldX, belowY, worldZ));
                        continue;
                    } else if (blockBelow >= 0 && blockBelow < registry.count() && !registry.get(blockBelow).isLiquid) {
                        // Solid block below - try horizontal spread
                        if (waterLevel < 7) {
                            uint8_t newLevel = waterLevel + 1;

                            // Check 4 horizontal neighbors
                            const glm::vec3 directions[4] = {
                                {0.5f, 0.0f, 0.0f},
                                {-0.5f, 0.0f, 0.0f},
                                {0.0f, 0.0f, 0.5f},
                                {0.0f, 0.0f, -0.5f}
                            };

                            for (const auto& dir : directions) {
                                float nx = worldX + dir.x;
                                float ny = worldY + dir.y;
                                float nz = worldZ + dir.z;

                                int neighborBlock = getBlockAt(nx, ny, nz);

                                if (neighborBlock == 0) {
                                    waterToAdd.push_back({nx, ny, nz, newLevel});
                                    chunksToUpdate.insert(getChunkAtWorldPos(nx, ny, nz));
                                } else if (neighborBlock >= 0 && neighborBlock < registry.count() && registry.get(neighborBlock).isLiquid) {
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

    // Pass 2: Apply all changes
    for (const auto& water : waterToAdd) {
        int existing = getBlockAt(water.x, water.y, water.z);
        if (existing == 0) {
            // Only place if still empty
            setBlockAt(water.x, water.y, water.z, 5); // Water block ID
            setBlockMetadataAt(water.x, water.y, water.z, water.level);
        } else if (existing >= 0 && existing < registry.count() && registry.get(existing).isLiquid) {
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
            if (blockID != 0 && blockID >= 0 && blockID < registry.count() && registry.get(blockID).isLiquid) {
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
            // PERFORMANCE FIX: Use batched upload instead of synchronous
            renderer->beginAsyncChunkUpload();
            chunk->createVertexBufferBatched(renderer);
            renderer->submitAsyncChunkUpload(chunk);
            updateCount++;
        }
    }
}

void World::updateWaterSimulation(float deltaTime, VulkanRenderer* renderer, const glm::vec3& playerPos, float renderDistance) {
    // Update particle system
    m_particleSystem->update(deltaTime);

    // Update water simulation with chunk freezing (only simulate water near player)
    m_waterSimulation->update(deltaTime, this, playerPos, renderDistance);

    // Particle spawning for water level changes is handled inside water simulation
    // See WaterSimulation::updateWaterCell() - spawns splash when water level increases

    // OPTIMIZATION: Only regenerate meshes for chunks where water actually changed
    // Using dirty chunk tracking (only updates chunks with water level changes)
    const auto& dirtyChunks = m_waterSimulation->getDirtyChunks();

    // Limit chunk updates per frame to prevent lag spikes (max 10 per frame)
    int updatesThisFrame = 0;
    const int maxUpdatesPerFrame = 10;  // Increased from 5 since we're only updating changed chunks

    for (const auto& chunkPos : dirtyChunks) {
        if (updatesThisFrame >= maxUpdatesPerFrame) break;

        Chunk* chunk = getChunkAt(chunkPos.x, chunkPos.y, chunkPos.z);
        if (chunk) {
            // Regenerate mesh for this chunk (water levels changed)
            chunk->generateMesh(this);

            // Upload to GPU (async to prevent frame stalls)
            renderer->beginAsyncChunkUpload();
            chunk->createVertexBufferBatched(renderer);
            renderer->submitAsyncChunkUpload(chunk);

            updatesThisFrame++;
        }
    }

    // Clear dirty chunks for next frame
    m_waterSimulation->clearDirtyChunks();
}

// ========== World Persistence ==========

bool World::saveWorld(const std::string& worldPath) const {
    namespace fs = std::filesystem;

    try {
        // Store world name and path for chunk streaming persistence
        const_cast<World*>(this)->m_worldName = fs::path(worldPath).filename().string();
        const_cast<World*>(this)->m_worldPath = worldPath;

        // Create world directory
        fs::create_directories(worldPath);
        Logger::info() << "Saving world to: " << worldPath;

        // Save world.meta file
        fs::path metaPath = fs::path(worldPath) / "world.meta";
        std::ofstream metaFile(metaPath, std::ios::binary);
        if (!metaFile.is_open()) {
            Logger::error() << "Failed to create world.meta file";
            return false;
        }

        // Write metadata header (version 2 - adds biome biases)
        constexpr uint32_t WORLD_FILE_VERSION = 2;
        metaFile.write(reinterpret_cast<const char*>(&WORLD_FILE_VERSION), sizeof(uint32_t));

        // Write world dimensions
        metaFile.write(reinterpret_cast<const char*>(&m_width), sizeof(int));
        metaFile.write(reinterpret_cast<const char*>(&m_height), sizeof(int));
        metaFile.write(reinterpret_cast<const char*>(&m_depth), sizeof(int));

        // Write world seed
        metaFile.write(reinterpret_cast<const char*>(&m_seed), sizeof(int));

        // Write world name length and name
        uint32_t nameLength = static_cast<uint32_t>(m_worldName.length());
        metaFile.write(reinterpret_cast<const char*>(&nameLength), sizeof(uint32_t));
        metaFile.write(m_worldName.c_str(), nameLength);

        // V2: Write biome biases
        metaFile.write(reinterpret_cast<const char*>(&m_temperatureBias), sizeof(float));
        metaFile.write(reinterpret_cast<const char*>(&m_moistureBias), sizeof(float));
        metaFile.write(reinterpret_cast<const char*>(&m_ageBias), sizeof(float));

        metaFile.close();
        Logger::info() << "World metadata saved successfully";

        // Save all loaded chunks
        int savedChunks = 0;
        int skippedChunks = 0;

        for (const auto& [coord, chunk] : m_chunkMap) {
            if (chunk && chunk->save(worldPath)) {
                savedChunks++;
            } else {
                skippedChunks++;
            }
        }

        // Also save cached chunks (includes unloaded but modified chunks)
        for (const auto& [coord, chunk] : m_unloadedChunksCache) {
            if (chunk && chunk->save(worldPath)) {
                savedChunks++;
            } else {
                skippedChunks++;
            }
        }

        Logger::info() << "World save complete - " << savedChunks << " chunks saved, "
                      << skippedChunks << " chunks skipped";
        return true;

    } catch (const std::exception& e) {
        Logger::error() << "Failed to save world: " << e.what();
        return false;
    }
}

int World::saveModifiedChunks() {
    if (m_worldPath.empty()) {
        Logger::warning() << "Cannot autosave - no world path set";
        return 0;
    }

    // THREAD SAFETY FIX: Lock while accessing m_dirtyChunks, m_chunkMap, and m_unloadedChunksCache
    // Without this, concurrent chunk loading/unloading during autosave causes crashes
    std::unique_lock<std::shared_mutex> lock(m_chunkMapMutex);

    int savedCount = 0;

    // Save dirty chunks from active chunks
    for (const auto& coord : m_dirtyChunks) {
        // Check active chunks first
        auto activeIt = m_chunkMap.find(coord);
        if (activeIt != m_chunkMap.end() && activeIt->second) {
            if (activeIt->second->save(m_worldPath)) {
                savedCount++;
            }
            continue;
        }

        // Check cache
        auto cacheIt = m_unloadedChunksCache.find(coord);
        if (cacheIt != m_unloadedChunksCache.end() && cacheIt->second) {
            if (cacheIt->second->save(m_worldPath)) {
                savedCount++;
            }
        }
    }

    // Clear dirty flags after successful save
    {
        std::lock_guard<std::mutex> dirtyLock(m_dirtyChunksMutex);
        m_dirtyChunks.clear();
    }

    if (savedCount > 0) {
        Logger::info() << "Autosave: saved " << savedCount << " modified chunks";
    }

    return savedCount;
}

void World::markChunkDirty(int chunkX, int chunkY, int chunkZ) {
    // THREAD SAFETY FIX (2025-11-23): Use dedicated mutex for m_dirtyChunks
    // This prevents parallel decoration from serializing on the chunk map mutex
    // PERFORMANCE: Fine-grained locking allows parallel threads to mark chunks dirty concurrently
    std::lock_guard<std::mutex> lock(m_dirtyChunksMutex);
    ChunkCoord coord{chunkX, chunkY, chunkZ};
    m_dirtyChunks.insert(coord);
}

void World::markChunkDirtyUnsafe(int chunkX, int chunkY, int chunkZ) {
    // NOTE (2025-11-23): Now safe to call even with m_chunkMapMutex held
    // m_dirtyChunks has its own dedicated mutex, no deadlock risk
    std::lock_guard<std::mutex> lock(m_dirtyChunksMutex);
    ChunkCoord coord{chunkX, chunkY, chunkZ};
    m_dirtyChunks.insert(coord);
}

std::unique_ptr<Chunk> World::getChunkFromCache(int chunkX, int chunkY, int chunkZ) {
    std::unique_lock<std::shared_mutex> lock(m_chunkMapMutex);

    ChunkCoord coord{chunkX, chunkY, chunkZ};
    auto it = m_unloadedChunksCache.find(coord);

    if (it != m_unloadedChunksCache.end()) {
        // Move chunk out of cache (caller takes ownership)
        std::unique_ptr<Chunk> chunk = std::move(it->second);
        m_unloadedChunksCache.erase(it);

        // FIXED (2025-11-23): Mark cached chunks as NOT needing decoration
        // These chunks were already decorated before being cached
        chunk->setNeedsDecoration(false);

        Logger::debug() << "Cache hit! Retrieved chunk (" << chunkX << ", " << chunkY << ", " << chunkZ
                       << ") from RAM cache (remaining cached: " << m_unloadedChunksCache.size() << ")";
        return chunk;
    }

    return nullptr;  // Cache miss
}

/**
 * @brief Acquires a chunk from the pool (or creates new if pool empty)
 *
 * CHUNK POOLING OPTIMIZATION:
 * Reusing chunk objects is ~100x faster than new/delete because:
 * - No memory allocation/deallocation overhead
 * - No constructor/destructor calls
 * - Reuses existing vector capacities (m_vertices, etc.)
 * - Better cache locality (chunks stay in same memory locations)
 */
std::unique_ptr<Chunk> World::acquireChunk(int chunkX, int chunkY, int chunkZ) {
    std::lock_guard<std::mutex> lock(m_chunkPoolMutex);
    if (!m_chunkPool.empty()) {
        // Reuse chunk from pool
        std::unique_ptr<Chunk> chunk = std::move(m_chunkPool.back());
        m_chunkPool.pop_back();
        chunk->reset(chunkX, chunkY, chunkZ);
        Logger::debug() << "Reused chunk from pool (" << chunkX << ", " << chunkY << ", " << chunkZ
                       << "), pool size: " << m_chunkPool.size();
        return chunk;
    } else {
        // Pool empty, create new chunk
        Logger::debug() << "Pool empty, creating new chunk (" << chunkX << ", " << chunkY << ", " << chunkZ << ")";
        return std::make_unique<Chunk>(chunkX, chunkY, chunkZ);
    }
}

/**
 * @brief Returns a chunk to the pool for reuse
 *
 * If pool is full, the chunk is simply destroyed. Otherwise, it's added to the
 * pool for fast reuse. Empty chunks are preferred for pooling (less memory usage).
 */
void World::releaseChunk(std::unique_ptr<Chunk> chunk) {
    std::lock_guard<std::mutex> lock(m_chunkPoolMutex);
    if (m_chunkPool.size() < m_maxPoolSize) {
        // Add to pool (empty chunks are lighter, but we pool all chunks)
        m_chunkPool.push_back(std::move(chunk));
        Logger::debug() << "Returned chunk to pool, pool size: " << m_chunkPool.size();
    } else {
        // Pool full, just destroy the chunk
        Logger::debug() << "Pool full, destroying chunk";
        chunk.reset();
    }
}

bool World::loadWorld(const std::string& worldPath) {
    namespace fs = std::filesystem;

    try {
        // Check if world directory exists
        if (!fs::exists(worldPath)) {
            Logger::error() << "World path does not exist: " << worldPath;
            return false;
        }

        // Load world.meta file
        fs::path metaPath = fs::path(worldPath) / "world.meta";
        if (!fs::exists(metaPath)) {
            Logger::error() << "world.meta file not found";
            return false;
        }

        std::ifstream metaFile(metaPath, std::ios::binary);
        if (!metaFile.is_open()) {
            Logger::error() << "Failed to open world.meta file";
            return false;
        }

        // Read and verify version
        uint32_t version;
        metaFile.read(reinterpret_cast<char*>(&version), sizeof(uint32_t));
        if (version != 1 && version != 2) {
            Logger::error() << "Unsupported world file version: " << version;
            return false;
        }

        // Read world dimensions
        int savedWidth, savedHeight, savedDepth, savedSeed;
        metaFile.read(reinterpret_cast<char*>(&savedWidth), sizeof(int));
        metaFile.read(reinterpret_cast<char*>(&savedHeight), sizeof(int));
        metaFile.read(reinterpret_cast<char*>(&savedDepth), sizeof(int));
        metaFile.read(reinterpret_cast<char*>(&savedSeed), sizeof(int));

        // Verify dimensions match current world
        if (savedWidth != m_width || savedHeight != m_height || savedDepth != m_depth) {
            Logger::error() << "World dimension mismatch - saved: " << savedWidth << "x" << savedHeight
                          << "x" << savedDepth << ", current: " << m_width << "x" << m_height << "x" << m_depth;
            return false;
        }

        // Update seed to match saved world
        m_seed = savedSeed;

        // Read world name
        uint32_t nameLength;
        metaFile.read(reinterpret_cast<char*>(&nameLength), sizeof(uint32_t));
        m_worldName.resize(nameLength);
        metaFile.read(&m_worldName[0], nameLength);

        // V2: Read biome biases (defaults to 0 for v1 saves)
        if (version >= 2) {
            metaFile.read(reinterpret_cast<char*>(&m_temperatureBias), sizeof(float));
            metaFile.read(reinterpret_cast<char*>(&m_moistureBias), sizeof(float));
            metaFile.read(reinterpret_cast<char*>(&m_ageBias), sizeof(float));
            Logger::info() << "Loaded biome biases: temp=" << m_temperatureBias
                          << ", moisture=" << m_moistureBias << ", age=" << m_ageBias;
        } else {
            // V1 save - use default biases
            m_temperatureBias = 0.0f;
            m_moistureBias = 0.0f;
            m_ageBias = 0.0f;
            Logger::info() << "V1 save file - using default biome biases";
        }

        metaFile.close();
        Logger::info() << "Loaded world metadata: " << m_worldName << " (seed: " << m_seed << ", version: " << version << ")";

        // Recreate biome map with loaded biases (required for consistent terrain generation)
        auto& biomeRegistry = BiomeRegistry::getInstance();
        auto [minTemp, maxTemp] = biomeRegistry.getTemperatureRange();
        auto [minMoisture, maxMoisture] = biomeRegistry.getMoistureRange();
        m_biomeMap = std::make_unique<BiomeMap>(m_seed, m_temperatureBias, m_moistureBias, m_ageBias,
                                                 minTemp, maxTemp, minMoisture, maxMoisture);
        Logger::info() << "Biome map recreated with saved biases";

        // Store world path for chunk streaming persistence
        m_worldPath = worldPath;

        // FIXED: Discover and load all chunk files from disk
        int loadedChunks = 0;
        fs::path chunksDir = fs::path(worldPath) / "chunks";

        if (fs::exists(chunksDir) && fs::is_directory(chunksDir)) {
            std::unique_lock<std::shared_mutex> lock(m_chunkMapMutex);

            for (const auto& entry : fs::directory_iterator(chunksDir)) {
                if (entry.path().extension() == ".chunk") {
                    // Parse chunk filename: "chunk_x_y_z.chunk"
                    std::string filename = entry.path().stem().string();
                    std::istringstream iss(filename);
                    std::string prefix;
                    int chunkX, chunkY, chunkZ;
                    char underscore;

                    if (iss >> prefix >> underscore >> chunkX >> underscore >> chunkY >> underscore >> chunkZ) {
                        // Create chunk and load from disk
                        ChunkCoord coord{chunkX, chunkY, chunkZ};
                        auto chunk = acquireChunk(chunkX, chunkY, chunkZ);

                        if (chunk->load(worldPath)) {
                            // MULTI-STAGE GENERATION FIX (2025-11-24): Mark loaded chunks as terrain ready
                            // Old save files don't have terrainReady flag, so we mark them ready on load
                            // This prevents deadlock where old chunks wait forever for old neighbors
                            chunk->setTerrainReady(true);

                            Chunk* chunkPtr = chunk.get();
                            m_chunkMap[coord] = std::move(chunk);
                            m_chunks.push_back(chunkPtr);
                            loadedChunks++;
                        }
                    }
                }
            }
        }

        Logger::info() << "World load complete - " << loadedChunks << " chunks loaded from disk";

        // MIGRATION FIX (2025-11-24): Mark all loaded chunks as terrain ready
        // This handles worlds saved before the terrainReady flag existed
        // Without this, old worlds would have all chunks stuck in decoration deadlock
        int migratedChunks = 0;
        for (Chunk* chunk : m_chunks) {
            if (chunk && !chunk->isTerrainReady()) {
                chunk->setTerrainReady(true);
                migratedChunks++;
            }
        }
        if (migratedChunks > 0) {
            Logger::info() << "Migration: Marked " << migratedChunks << " existing chunks as terrain-ready";
        }

        return true;

    } catch (const std::exception& e) {
        Logger::error() << "Failed to load world: " << e.what();
        return false;
    }
}

std::string World::getWorldName() const {
    return m_worldName.empty() ? "Unnamed World" : m_worldName;
}

bool World::isChunkInBounds(int chunkX, int chunkY, int chunkZ) const {
    int halfWidth = m_width / 2;
    int halfHeight = m_height / 2;
    int halfDepth = m_depth / 2;

    return (chunkX >= -halfWidth && chunkX < halfWidth &&
            chunkY >= -halfHeight && chunkY < halfHeight &&
            chunkZ >= -halfDepth && chunkZ < halfDepth);
}
