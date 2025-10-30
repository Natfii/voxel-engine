#include "world.h"
#include "vulkan_renderer.h"
#include <glm/glm.hpp>

World::World(int width, int height, int depth)
    : m_width(width), m_height(height), m_depth(depth) {
    // Center world generation around origin (0, 0, 0)
    int halfWidth = width / 2;
    int halfDepth = depth / 2;

    for (int x = -halfWidth; x < width - halfWidth; ++x) {
        for (int y = 0; y < height; ++y) {
            for (int z = -halfDepth; z < depth - halfDepth; ++z) {
                m_chunks.push_back(new Chunk(x, y, z));
            }
        }
    }
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
    for (Chunk* chunk : m_chunks) {
        chunk->generate();
    }
}

void World::createBuffers(VulkanRenderer* renderer) {
    for (Chunk* chunk : m_chunks) {
        chunk->createVertexBuffer(renderer);
    }
}

void World::renderWorld(VkCommandBuffer commandBuffer, const glm::vec3& cameraPos, float renderDistance) {
    // Distance culling only

    for (Chunk* chunk : m_chunks) {
        // Distance culling: skip chunks beyond render distance
        glm::vec3 chunkCenter = chunk->getCenter();
        float distance = glm::length(chunkCenter - cameraPos);

        if (distance > renderDistance) {
            continue; // Too far away
        }

        // Render chunk
        chunk->render(commandBuffer);
    }
}
