#include "world.h"
#include "vulkan_renderer.h"
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

void World::renderWorld(VkCommandBuffer commandBuffer, const glm::vec3& cameraPos, float renderDistance) {
    // Validate camera position for NaN/Inf to prevent rendering errors
    if (!std::isfinite(cameraPos.x) || !std::isfinite(cameraPos.y) || !std::isfinite(cameraPos.z)) {
        return; // Skip rendering if camera position is invalid
    }

    // Simple distance-based culling with fixed threshold
    // Add small margin (1 unit) to prevent floating-point boundary flickering
    const float renderDistanceWithMargin = renderDistance + 1.0f;
    const float renderDistanceSquared = renderDistanceWithMargin * renderDistanceWithMargin;

    int renderedCount = 0;
    int culledCount = 0;

    for (Chunk* chunk : m_chunks) {
        // Skip chunks with no vertices (optimization)
        if (chunk->getVertexCount() == 0) {
            continue;
        }

        // Compute distance to chunk center
        glm::vec3 delta = chunk->getCenter() - cameraPos;
        float distanceSquared = glm::dot(delta, delta);

        // Simple visibility check - always render if within distance
        if (distanceSquared <= renderDistanceSquared) {
            chunk->render(commandBuffer);
            renderedCount++;
        } else {
            culledCount++;
        }
    }

    // Debug output every 60 frames (roughly once per second at 60 FPS)
    static int frameCount = 0;
    if (frameCount++ % 60 == 0) {
        std::cout << "Rendered: " << renderedCount << " chunks, Culled: " << culledCount
                  << " chunks, Total chunks: " << m_chunks.size() << std::endl;
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
