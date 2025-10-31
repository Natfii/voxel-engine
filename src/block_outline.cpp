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
    const float offset = 0.005f; // Small offset to prevent z-fighting (combined with depth bias)

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
    // Create initial buffer with dummy data
    std::vector<float> dummyVerts = createOutlineVertices(0, 0, 0);

    VkDeviceSize bufferSize = sizeof(float) * dummyVerts.size();

    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    renderer->createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          stagingBuffer, stagingBufferMemory);

    // Copy vertex data to staging buffer
    void* data;
    vkMapMemory(renderer->getDevice(), stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, dummyVerts.data(), (size_t)bufferSize);
    vkUnmapMemory(renderer->getDevice(), stagingBufferMemory);

    // Create vertex buffer on device
    renderer->createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          m_vertexBuffer, m_vertexBufferMemory);

    // Copy from staging to device
    renderer->copyBuffer(stagingBuffer, m_vertexBuffer, bufferSize);

    // Cleanup staging buffer
    vkDestroyBuffer(renderer->getDevice(), stagingBuffer, nullptr);
    vkFreeMemory(renderer->getDevice(), stagingBufferMemory, nullptr);

    m_vertexCount = dummyVerts.size() / 6; // 6 floats per vertex (xyz rgb)
}

void BlockOutline::setPosition(float x, float y, float z) {
    m_position = glm::vec3(x, y, z);
    m_visible = true;
}

void BlockOutline::updateBuffer(VulkanRenderer* renderer) {
    // Create new vertex data
    std::vector<float> verts = createOutlineVertices(m_position.x, m_position.y, m_position.z);

    VkDeviceSize bufferSize = sizeof(float) * verts.size();

    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    renderer->createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          stagingBuffer, stagingBufferMemory);

    // Copy vertex data to staging buffer
    void* data;
    vkMapMemory(renderer->getDevice(), stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, verts.data(), (size_t)bufferSize);
    vkUnmapMemory(renderer->getDevice(), stagingBufferMemory);

    // Copy from staging to device buffer
    renderer->copyBuffer(stagingBuffer, m_vertexBuffer, bufferSize);

    // Cleanup staging buffer
    vkDestroyBuffer(renderer->getDevice(), stagingBuffer, nullptr);
    vkFreeMemory(renderer->getDevice(), stagingBufferMemory, nullptr);
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
