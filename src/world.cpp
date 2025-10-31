#include "world.h"
#include "vulkan_renderer.h"
#include "frustum.h"
#include "debug_state.h"
#include <glm/glm.hpp>
#include <thread>
#include <algorithm>
#include <cmath>
#include <iostream>

World::World(int width, int height, int depth)
    : m_width(width), m_height(height), m_depth(depth) {
    // Center world generation around origin (0, 0, 0)
    int halfWidth = width / 2;
    int halfDepth = depth / 2;

    std::cout << "Creating world with " << width << "x" << height << "x" << depth << " chunks" << std::endl;
    std::cout << "Chunk coordinates range: X[" << -halfWidth << " to " << (width - halfWidth - 1)
              << "], Y[0 to " << (height - 1) << "], Z[" << -halfDepth << " to " << (depth - halfDepth - 1) << "]" << std::endl;

    for (int x = -halfWidth; x < width - halfWidth; ++x) {
        for (int y = 0; y < height; ++y) {
            for (int z = -halfDepth; z < depth - halfDepth; ++z) {
                m_chunks.push_back(new Chunk(x, y, z));
            }
        }
    }

    std::cout << "Total chunks created: " << m_chunks.size() << std::endl;
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

    // Wait for all threads to complete
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

void World::renderWorld(VkCommandBuffer commandBuffer, const glm::vec3& cameraPos, const glm::mat4& viewProj, float renderDistance) {
    // Validate camera position for NaN/Inf to prevent rendering errors
    if (!std::isfinite(cameraPos.x) || !std::isfinite(cameraPos.y) || !std::isfinite(cameraPos.z)) {
        return; // Skip rendering if camera position is invalid
    }

    // Extract frustum from view-projection matrix
    Frustum frustum = extractFrustum(viewProj);

    // Chunk culling: account for chunk size to prevent popping
    // Chunks are 32x32x32 blocks = 16x16x16 world units
    // Distance from chunk center to farthest corner = sqrt(8^2 + 8^2 + 8^2) ≈ 13.86 units
    // Fragment shader discards at renderDistance * 1.05 (see shader.frag)
    // Render chunks if their farthest corner could be visible
    const float fragmentDiscardDistance = renderDistance * 1.05f;
    const float chunkHalfDiagonal = 13.86f;
    const float renderDistanceWithMargin = fragmentDiscardDistance + chunkHalfDiagonal;
    const float renderDistanceSquared = renderDistanceWithMargin * renderDistanceWithMargin;

    // Frustum margin: add extra padding to prevent edge-case popping
    // Slightly larger than default to account for chunk size (16 world units)
    const float frustumMargin = chunkHalfDiagonal + 2.0f;

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

    // Debug output every 60 frames (roughly once per second at 60 FPS)
    static int frameCount = 0;
    if (frameCount++ % 60 == 0) {
        std::cout << "Rendered: " << renderedCount << " chunks | "
                  << "Distance culled: " << distanceCulled << " | "
                  << "Frustum culled: " << frustumCulled << " | "
                  << "Total: " << m_chunks.size() << " chunks" << std::endl;
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
    // Convert world coordinates to chunk coordinates
    // Blocks are 0.5 units in size, and each chunk is 32 blocks
    int blockX = static_cast<int>(std::floor(worldX / 0.5f));
    int blockY = static_cast<int>(std::floor(worldY / 0.5f));
    int blockZ = static_cast<int>(std::floor(worldZ / 0.5f));

    int chunkX = blockX / Chunk::WIDTH;
    int chunkY = blockY / Chunk::HEIGHT;
    int chunkZ = blockZ / Chunk::DEPTH;

    int localX = blockX - (chunkX * Chunk::WIDTH);
    int localY = blockY - (chunkY * Chunk::HEIGHT);
    int localZ = blockZ - (chunkZ * Chunk::DEPTH);

    // Handle negative coordinates properly
    if (localX < 0) { localX += Chunk::WIDTH; chunkX--; }
    if (localY < 0) { localY += Chunk::HEIGHT; chunkY--; }
    if (localZ < 0) { localZ += Chunk::DEPTH; chunkZ--; }

    Chunk* chunk = getChunkAt(chunkX, chunkY, chunkZ);
    if (chunk == nullptr) {
        return 0; // Air (outside world bounds)
    }

    return chunk->getBlock(localX, localY, localZ);
}

Chunk* World::getChunkAtWorldPos(float worldX, float worldY, float worldZ) {
    // Convert world coordinates to chunk coordinates (same logic as getBlockAt)
    int blockX = static_cast<int>(std::floor(worldX / 0.5f));
    int blockY = static_cast<int>(std::floor(worldY / 0.5f));
    int blockZ = static_cast<int>(std::floor(worldZ / 0.5f));

    int chunkX = blockX / Chunk::WIDTH;
    int chunkY = blockY / Chunk::HEIGHT;
    int chunkZ = blockZ / Chunk::DEPTH;

    int localX = blockX - (chunkX * Chunk::WIDTH);
    int localY = blockY - (chunkY * Chunk::HEIGHT);
    int localZ = blockZ - (chunkZ * Chunk::DEPTH);

    // Handle negative coordinates properly
    if (localX < 0) { localX += Chunk::WIDTH; chunkX--; }
    if (localY < 0) { localY += Chunk::HEIGHT; chunkY--; }
    if (localZ < 0) { localZ += Chunk::DEPTH; chunkZ--; }

    return getChunkAt(chunkX, chunkY, chunkZ);
}

void World::setBlockAt(float worldX, float worldY, float worldZ, int blockID) {
    // Convert world coordinates to chunk coordinates (same logic as getBlockAt)
    int blockX = static_cast<int>(std::floor(worldX / 0.5f));
    int blockY = static_cast<int>(std::floor(worldY / 0.5f));
    int blockZ = static_cast<int>(std::floor(worldZ / 0.5f));

    int chunkX = blockX / Chunk::WIDTH;
    int chunkY = blockY / Chunk::HEIGHT;
    int chunkZ = blockZ / Chunk::DEPTH;

    int localX = blockX - (chunkX * Chunk::WIDTH);
    int localY = blockY - (chunkY * Chunk::HEIGHT);
    int localZ = blockZ - (chunkZ * Chunk::DEPTH);

    // Handle negative coordinates properly
    if (localX < 0) { localX += Chunk::WIDTH; chunkX--; }
    if (localY < 0) { localY += Chunk::HEIGHT; chunkY--; }
    if (localZ < 0) { localZ += Chunk::DEPTH; chunkZ--; }

    Chunk* chunk = getChunkAt(chunkX, chunkY, chunkZ);
    if (chunk == nullptr) {
        return; // Outside world bounds, do nothing
    }

    // Set the block
    chunk->setBlock(localX, localY, localZ, blockID);

    // TODO: For block break animation, we can add a callback here later
    // For now, immediately update the mesh
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
