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

private:
    int m_width, m_height, m_depth;
    std::vector<Chunk*> m_chunks;

    int index(int x, int y, int z) const;
};
