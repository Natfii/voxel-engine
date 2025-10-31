#pragma once
#include <vector>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include "chunk.h"

// Forward declaration
class VulkanRenderer;

class World {
public:
    World(int width, int height, int depth);
    ~World();

    void generateWorld();
    void createBuffers(VulkanRenderer* renderer);
    void renderWorld(VkCommandBuffer commandBuffer, const glm::vec3& cameraPos, float renderDistance = 50.0f);

    // Block querying for raycasting
    Chunk* getChunkAt(int chunkX, int chunkY, int chunkZ);
    int getBlockAt(float worldX, float worldY, float worldZ);

private:
    int m_width, m_height, m_depth;
    std::vector<Chunk*> m_chunks;

    int index(int x, int y, int z) const;
};
