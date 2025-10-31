#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>

// Forward declaration
class VulkanRenderer;

class BlockOutline {
public:
    BlockOutline();
    ~BlockOutline();

    void init(VulkanRenderer* renderer);
    void cleanup(VulkanRenderer* renderer);

    // Update the outline position (in world coordinates)
    void setPosition(float x, float y, float z);
    void setVisible(bool visible) { m_visible = visible; }

    // Render the outline
    void render(VkCommandBuffer commandBuffer);

private:
    bool m_visible;
    glm::vec3 m_position;

    VkBuffer m_vertexBuffer;
    VkDeviceMemory m_vertexBufferMemory;
    uint32_t m_vertexCount;

    std::vector<float> createOutlineVertices(float x, float y, float z);
};
