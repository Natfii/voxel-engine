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

    // Block querying and modification
    Chunk* getChunkAt(int chunkX, int chunkY, int chunkZ);
    Chunk* getChunkAtWorldPos(float worldX, float worldY, float worldZ);  // Get chunk containing world position
    int getBlockAt(float worldX, float worldY, float worldZ);
    void setBlockAt(float worldX, float worldY, float worldZ, int blockID);  // Set block (0 = air/remove)

private:
    int m_width, m_height, m_depth;
    std::vector<Chunk*> m_chunks;

    int index(int x, int y, int z) const;
};
