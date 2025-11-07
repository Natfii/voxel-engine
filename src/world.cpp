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
#include "vulkan_renderer.h"
#include "frustum.h"
#include "debug_state.h"
#include "logger.h"
#include "block_system.h"
#include <glm/glm.hpp>
#include <thread>
#include <algorithm>
#include <cmath>

World::World(int width, int height, int depth)
    : m_width(width), m_height(height), m_depth(depth) {
    // Center world generation around origin (0, 0, 0)
    int halfWidth = width / 2;
    int halfDepth = depth / 2;

    Logger::info() << "Creating world with " << width << "x" << height << "x" << depth << " chunks";
    Logger::info() << "Chunk coordinates range: X[" << -halfWidth << " to " << (width - halfWidth - 1)
                   << "], Y[0 to " << (height - 1) << "], Z[" << -halfDepth << " to " << (depth - halfDepth - 1) << "]";

    for (int x = -halfWidth; x < width - halfWidth; ++x) {
        for (int y = 0; y < height; ++y) {
            for (int z = -halfDepth; z < depth - halfDepth; ++z) {
                m_chunks.push_back(new Chunk(x, y, z));
            }
        }
    }

    Logger::info() << "Total chunks created: " << m_chunks.size();
}

World::~World() {
    for (Chunk* chunk : m_chunks) {
        delete chunk;
    }
}

int World::index(int x, int y, int z) const {
    return x + m_width * (y + m_height * z);
}

void World::generateWorld() {
    // Parallel chunk generation for better performance
    const unsigned int numThreads = std::thread::hardware_concurrency();
    const size_t chunksPerThread = (m_chunks.size() + numThreads - 1) / numThreads;

    std::vector<std::thread> threads;
    threads.reserve(numThreads);

    // Step 1: Generate terrain blocks in parallel
    for (unsigned int i = 0; i < numThreads; ++i) {
        size_t startIdx = i * chunksPerThread;
        size_t endIdx = std::min(startIdx + chunksPerThread, m_chunks.size());

        if (startIdx >= m_chunks.size()) break;

        threads.emplace_back([this, startIdx, endIdx]() {
            for (size_t j = startIdx; j < endIdx; ++j) {
                m_chunks[j]->generate();
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

void World::createBuffers(VulkanRenderer* renderer) {
    // Only create buffers for chunks with vertices (skip empty chunks)
    for (Chunk* chunk : m_chunks) {
        if (chunk->getVertexCount() > 0) {
            chunk->createVertexBuffer(renderer);
        }
    }
}

void World::cleanup(VulkanRenderer* renderer) {
    // Destroy all chunk buffers before deleting chunks
    for (Chunk* chunk : m_chunks) {
        chunk->destroyBuffers(renderer);
    }
}

void World::renderWorld(VkCommandBuffer commandBuffer, const glm::vec3& cameraPos, const glm::mat4& viewProj, float renderDistance) {
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

    for (Chunk* chunk : m_chunks) {
        // Skip chunks with no vertices (optimization)
        if (chunk->getVertexCount() == 0) {
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

        // Chunk passed all culling tests - render it
        chunk->render(commandBuffer);
        renderedCount++;
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

Chunk* World::getChunkAt(int chunkX, int chunkY, int chunkZ) {
    // Find chunk with matching coordinates
    for (Chunk* chunk : m_chunks) {
        if (chunk->getChunkX() == chunkX &&
            chunk->getChunkY() == chunkY &&
            chunk->getChunkZ() == chunkZ) {
            return chunk;
        }
    }
    return nullptr;
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

Chunk* World::getChunkAtWorldPos(float worldX, float worldY, float worldZ) {
    // Convert world coordinates to chunk coordinates
    auto coords = worldToBlockCoords(worldX, worldY, worldZ);
    return getChunkAt(coords.chunkX, coords.chunkY, coords.chunkZ);
}

void World::setBlockAt(float worldX, float worldY, float worldZ, int blockID) {
    // Convert world coordinates to chunk and local block coordinates
    auto coords = worldToBlockCoords(worldX, worldY, worldZ);

    Chunk* chunk = getChunkAt(coords.chunkX, coords.chunkY, coords.chunkZ);
    if (chunk == nullptr) {
        return; // Outside world bounds, do nothing
    }

    // Set the block
    chunk->setBlock(coords.localX, coords.localY, coords.localZ, blockID);

    // NOTE: Block break animations could be added in the future by:
    // - Adding a callback parameter to this function
    // - Delaying mesh regeneration until animation completes
    // - Using a particle system for break effects
    // For now, blocks update instantly (Minecraft-style instant break)
    chunk->generateMesh(this);
    // Note: We don't call createBuffer here - that needs a renderer which we don't have access to
    // We'll mark the chunk as needing a buffer update elsewhere
}

void World::breakBlock(float worldX, float worldY, float worldZ, VulkanRenderer* renderer) {
    // Break the block (set to air = 0)
    setBlockAt(worldX, worldY, worldZ, 0);

    // Update the affected chunk and all adjacent chunks
    // Must regenerate MESH (not just vertex buffer) because face culling needs updating
    Chunk* affectedChunk = getChunkAtWorldPos(worldX, worldY, worldZ);
    if (affectedChunk) {
        affectedChunk->generateMesh(this);
        affectedChunk->createVertexBuffer(renderer);
    }

    // Always update all 6 adjacent chunks (not just on boundaries)
    // This handles cases like breaking grass revealing stone below
    Chunk* neighbors[6] = {
        getChunkAtWorldPos(worldX - 0.5f, worldY, worldZ),  // -X
        getChunkAtWorldPos(worldX + 0.5f, worldY, worldZ),  // +X
        getChunkAtWorldPos(worldX, worldY - 0.5f, worldZ),  // -Y (below)
        getChunkAtWorldPos(worldX, worldY + 0.5f, worldZ),  // +Y (above)
        getChunkAtWorldPos(worldX, worldY, worldZ - 0.5f),  // -Z
        getChunkAtWorldPos(worldX, worldY, worldZ + 0.5f)   // +Z
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
                neighbors[i]->generateMesh(this);
                neighbors[i]->createVertexBuffer(renderer);
            }
        }
    }
}

void World::breakBlock(const glm::vec3& position, VulkanRenderer* renderer) {
    breakBlock(position.x, position.y, position.z, renderer);
}

void World::breakBlock(const glm::ivec3& coords, VulkanRenderer* renderer) {
    // Convert block coordinates to world coordinates
    float worldX = coords.x * 0.5f;
    float worldY = coords.y * 0.5f;
    float worldZ = coords.z * 0.5f;
    breakBlock(worldX, worldY, worldZ, renderer);
}

void World::updateLiquids(VulkanRenderer* renderer) {
    auto& registry = BlockRegistry::instance();
    std::vector<Chunk*> chunksToUpdate;

    // Iterate through all chunks from bottom to top (so liquids fall properly)
    for (int y = 0; y < m_height; ++y) {
        for (int x = 0; x < m_width; ++x) {
            for (int z = 0; z < m_depth; ++z) {
                // Get chunk using proper index calculation
                int halfWidth = m_width / 2;
                int halfDepth = m_depth / 2;
                int chunkX = x - halfWidth;
                int chunkZ = z - halfDepth;

                Chunk* chunk = getChunkAt(chunkX, y, chunkZ);
                if (!chunk) continue;

                bool chunkModified = false;

                // Iterate through blocks in this chunk
                for (int localX = 0; localX < Chunk::WIDTH; ++localX) {
                    for (int localY = Chunk::HEIGHT - 1; localY >= 0; --localY) {  // Top to bottom
                        for (int localZ = 0; localZ < Chunk::DEPTH; ++localZ) {
                            int blockID = chunk->getBlock(localX, localY, localZ);
                            if (blockID == 0) continue;

                            // Check if this is a liquid block
                            const BlockDefinition& def = registry.get(blockID);
                            if (!def.isLiquid) continue;

                            // Calculate world position
                            float worldX = (chunkX * Chunk::WIDTH + localX) * 0.5f;
                            float worldY = (y * Chunk::HEIGHT + localY) * 0.5f;
                            float worldZ = (chunkZ * Chunk::DEPTH + localZ) * 0.5f;

                            // Check block below
                            float belowY = worldY - 0.5f;
                            int blockBelow = getBlockAt(worldX, belowY, worldZ);

                            // If there's air below, make liquid fall
                            if (blockBelow == 0) {
                                // Remove liquid from current position
                                setBlockAt(worldX, worldY, worldZ, 0);
                                // Place liquid one block below
                                setBlockAt(worldX, belowY, worldZ, blockID);
                                chunkModified = true;

                                // Mark the chunk below for update too
                                Chunk* chunkBelow = getChunkAtWorldPos(worldX, belowY, worldZ);
                                if (chunkBelow && std::find(chunksToUpdate.begin(), chunksToUpdate.end(), chunkBelow) == chunksToUpdate.end()) {
                                    chunksToUpdate.push_back(chunkBelow);
                                }
                            }
                        }
                    }
                }

                // Mark this chunk for mesh update if modified
                if (chunkModified && std::find(chunksToUpdate.begin(), chunksToUpdate.end(), chunk) == chunksToUpdate.end()) {
                    chunksToUpdate.push_back(chunk);
                }
            }
        }
    }

    // Regenerate meshes for all affected chunks
    for (Chunk* chunk : chunksToUpdate) {
        chunk->generateMesh(this);
        chunk->createVertexBuffer(renderer);
    }
}
