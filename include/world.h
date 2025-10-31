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

    // Higher-level block operations
    void breakBlock(float worldX, float worldY, float worldZ, VulkanRenderer* renderer);  // Break block and update meshes
    void breakBlock(const glm::vec3& position, VulkanRenderer* renderer);  // Convenience overload
    void breakBlock(const glm::ivec3& coords, VulkanRenderer* renderer);   // Break by block coords

private:
    int m_width, m_height, m_depth;
    std::vector<Chunk*> m_chunks;

    int index(int x, int y, int z) const;
};
