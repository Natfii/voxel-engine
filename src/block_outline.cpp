#include "block_outline.h"
#include "vulkan_renderer.h"
#include "chunk.h"
#include <cstring>

BlockOutline::BlockOutline()
    : m_visible(false), m_position(0.0f, 0.0f, 0.0f),
      m_vertexBuffer(VK_NULL_HANDLE), m_vertexBufferMemory(VK_NULL_HANDLE), m_vertexCount(0) {
}

BlockOutline::~BlockOutline() {
}

std::vector<float> BlockOutline::createOutlineVertices(float x, float y, float z) {
    // Create a wireframe cube with 12 edges (24 vertices for lines)
    // Each block is 0.5 units in size
    const float size = 0.5f;
    const float offset = 0.001f; // Small offset to prevent z-fighting

    std::vector<float> vertices;
    vertices.reserve(24 * 6); // 24 vertices * 6 floats per vertex (x,y,z,r,g,b)

    // Helper lambda to add a line (2 vertices)
    auto addLine = [&](float x1, float y1, float z1, float x2, float y2, float z2) {
        // First vertex
        vertices.push_back(x1);
        vertices.push_back(y1);
        vertices.push_back(z1);
        vertices.push_back(0.0f); // Black outline
        vertices.push_back(0.0f);
        vertices.push_back(0.0f);

        // Second vertex
        vertices.push_back(x2);
        vertices.push_back(y2);
        vertices.push_back(z2);
        vertices.push_back(0.0f); // Black outline
        vertices.push_back(0.0f);
        vertices.push_back(0.0f);
    };

    // Slightly expand the outline to make it visible
    float x0 = x - offset;
    float y0 = y - offset;
    float z0 = z - offset;
    float x1 = x + size + offset;
    float y1 = y + size + offset;
    float z1 = z + size + offset;

    // Bottom face edges
    addLine(x0, y0, z0, x1, y0, z0); // Front
    addLine(x1, y0, z0, x1, y0, z1); // Right
    addLine(x1, y0, z1, x0, y0, z1); // Back
    addLine(x0, y0, z1, x0, y0, z0); // Left

    // Top face edges
    addLine(x0, y1, z0, x1, y1, z0); // Front
    addLine(x1, y1, z0, x1, y1, z1); // Right
    addLine(x1, y1, z1, x0, y1, z1); // Back
    addLine(x0, y1, z1, x0, y1, z0); // Left

    // Vertical edges
    addLine(x0, y0, z0, x0, y1, z0); // Front-left
    addLine(x1, y0, z0, x1, y1, z0); // Front-right
    addLine(x1, y0, z1, x1, y1, z1); // Back-right
    addLine(x0, y0, z1, x0, y1, z1); // Back-left

    return vertices;
}

void BlockOutline::init(VulkanRenderer* renderer) {
    // Initial buffer will be created when position is first set
}

void BlockOutline::setPosition(float x, float y, float z) {
    m_position = glm::vec3(x, y, z);
    m_visible = true;
    // Buffer update will happen in render()
}

void BlockOutline::render(VkCommandBuffer commandBuffer) {
    if (!m_visible || m_vertexBuffer == VK_NULL_HANDLE) {
        return;
    }

    VkBuffer vertexBuffers[] = {m_vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdDraw(commandBuffer, m_vertexCount, 1, 0, 0);
}

void BlockOutline::cleanup(VulkanRenderer* renderer) {
    if (m_vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(renderer->getDevice(), m_vertexBuffer, nullptr);
        m_vertexBuffer = VK_NULL_HANDLE;
    }
    if (m_vertexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(renderer->getDevice(), m_vertexBufferMemory, nullptr);
        m_vertexBufferMemory = VK_NULL_HANDLE;
    }
}
