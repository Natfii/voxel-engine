#include "targeting_system.h"
#include "raycast.h"
#include "world.h"
#include "block_system.h"
#include "vulkan_renderer.h"
#include "imgui.h"
#include <cstring>

TargetingSystem::TargetingSystem()
    : m_enabled(true),
      m_maxDistance(2.5f),
      m_crosshairVisible(true),
      m_crosshairSize(10.0f),
      m_crosshairThickness(2.0f),
      m_crosshairGap(3.0f),
      m_outlineVisible(true),
      m_outlineVertexBuffer(VK_NULL_HANDLE),
      m_outlineVertexBufferMemory(VK_NULL_HANDLE),
      m_outlineVertexCount(0) {
}

TargetingSystem::~TargetingSystem() {
}

void TargetingSystem::init(VulkanRenderer* renderer) {
    // Create initial outline buffer with dummy data
    std::vector<float> dummyVerts = createOutlineVertices(glm::vec3(0, 0, 0));

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
                          m_outlineVertexBuffer, m_outlineVertexBufferMemory);

    // Copy from staging to device
    renderer->copyBuffer(stagingBuffer, m_outlineVertexBuffer, bufferSize);

    // Cleanup staging buffer
    vkDestroyBuffer(renderer->getDevice(), stagingBuffer, nullptr);
    vkFreeMemory(renderer->getDevice(), stagingBufferMemory, nullptr);

    m_outlineVertexCount = dummyVerts.size() / 8; // 8 floats per vertex (xyz rgb uv)
}

void TargetingSystem::cleanup(VulkanRenderer* renderer) {
    if (m_outlineVertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(renderer->getDevice(), m_outlineVertexBuffer, nullptr);
        m_outlineVertexBuffer = VK_NULL_HANDLE;
    }
    if (m_outlineVertexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(renderer->getDevice(), m_outlineVertexBufferMemory, nullptr);
        m_outlineVertexBufferMemory = VK_NULL_HANDLE;
    }
}

void TargetingSystem::update(World* world, const glm::vec3& playerPos, const glm::vec3& direction) {
    if (!m_enabled) {
        m_currentTarget = TargetInfo(); // Clear target
        return;
    }

    // Cast ray to find target
    RaycastHit hit = Raycast::castRay(world, playerPos, direction, m_maxDistance);

    // Populate target info
    m_currentTarget.hasTarget = hit.hit;
    if (hit.hit) {
        m_currentTarget.blockPosition = hit.position;
        m_currentTarget.hitNormal = hit.normal;
        m_currentTarget.blockCoords = glm::ivec3(hit.blockX, hit.blockY, hit.blockZ);
        m_currentTarget.distance = hit.distance;

        // Get block data
        m_currentTarget.blockID = world->getBlockAt(hit.position.x, hit.position.y, hit.position.z);
        updateTargetInfo(world);
    } else {
        m_currentTarget.blockID = 0;
        m_currentTarget.blockName = "";
        m_currentTarget.blockType = "air";
        m_currentTarget.isBreakable = false;
    }
}

void TargetingSystem::updateTargetInfo(World* world) {
    // Query block information from BlockRegistry
    m_currentTarget.blockName = BlockRegistry::instance().getBlockName(m_currentTarget.blockID);
    m_currentTarget.blockType = BlockRegistry::instance().getBlockType(m_currentTarget.blockID);
    m_currentTarget.isBreakable = BlockRegistry::instance().isBreakable(m_currentTarget.blockID);
}

void TargetingSystem::renderCrosshair() {
    if (!m_crosshairVisible || !m_enabled) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 displaySize = io.DisplaySize;

    float centerX = displaySize.x * 0.5f;
    float centerY = displaySize.y * 0.5f;

    // Create an invisible window that covers the entire screen
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(displaySize, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0)); // Fully transparent

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
                           | ImGuiWindowFlags_NoMove
                           | ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoInputs
                           | ImGuiWindowFlags_NoSavedSettings
                           | ImGuiWindowFlags_NoFocusOnAppearing
                           | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("Crosshair", nullptr, flags);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImU32 color = IM_COL32(0, 0, 0, 191); // Black with 75% opacity

    // Draw horizontal line (left and right of center)
    drawList->AddLine(
        ImVec2(centerX - m_crosshairSize - m_crosshairGap, centerY),
        ImVec2(centerX - m_crosshairGap, centerY),
        color,
        m_crosshairThickness
    );
    drawList->AddLine(
        ImVec2(centerX + m_crosshairGap, centerY),
        ImVec2(centerX + m_crosshairSize + m_crosshairGap, centerY),
        color,
        m_crosshairThickness
    );

    // Draw vertical line (top and bottom of center)
    drawList->AddLine(
        ImVec2(centerX, centerY - m_crosshairSize - m_crosshairGap),
        ImVec2(centerX, centerY - m_crosshairGap),
        color,
        m_crosshairThickness
    );
    drawList->AddLine(
        ImVec2(centerX, centerY + m_crosshairGap),
        ImVec2(centerX, centerY + m_crosshairSize + m_crosshairGap),
        color,
        m_crosshairThickness
    );

    ImGui::End();
    ImGui::PopStyleColor();
}

void TargetingSystem::renderBlockOutline(VkCommandBuffer commandBuffer) {
    if (!m_outlineVisible || !m_enabled || !m_currentTarget.hasTarget) {
        return;
    }

    if (m_outlineVertexBuffer == VK_NULL_HANDLE) {
        return;
    }

    VkBuffer vertexBuffers[] = {m_outlineVertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdDraw(commandBuffer, m_outlineVertexCount, 1, 0, 0);
}

void TargetingSystem::updateOutlineBuffer(VulkanRenderer* renderer) {
    if (!m_currentTarget.hasTarget) {
        return;
    }

    // Create new vertex data for current target position
    std::vector<float> verts = createOutlineVertices(m_currentTarget.blockPosition);

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
    renderer->copyBuffer(stagingBuffer, m_outlineVertexBuffer, bufferSize);

    // Cleanup staging buffer
    vkDestroyBuffer(renderer->getDevice(), stagingBuffer, nullptr);
    vkFreeMemory(renderer->getDevice(), stagingBufferMemory, nullptr);
}

std::vector<float> TargetingSystem::createOutlineVertices(const glm::vec3& position) {
    // Create a wireframe cube with 12 edges (24 vertices for lines)
    const float size = 0.5f;
    const float inset = 0.002f; // Small inset to render on inside edges (prevents clipping)

    std::vector<float> vertices;
    vertices.reserve(24 * 8); // 24 vertices * 8 floats per vertex (x,y,z,r,g,b,u,v)

    // Helper lambda to add a line (2 vertices)
    auto addLine = [&](float x1, float y1, float z1, float x2, float y2, float z2) {
        // First vertex
        vertices.push_back(x1);
        vertices.push_back(y1);
        vertices.push_back(z1);
        vertices.push_back(0.0f); // Black outline color
        vertices.push_back(0.0f);
        vertices.push_back(0.0f);
        vertices.push_back(0.0f); // UV coordinates
        vertices.push_back(0.0f);

        // Second vertex
        vertices.push_back(x2);
        vertices.push_back(y2);
        vertices.push_back(z2);
        vertices.push_back(0.0f); // Black outline color
        vertices.push_back(0.0f);
        vertices.push_back(0.0f);
        vertices.push_back(0.0f); // UV coordinates
        vertices.push_back(0.0f);
    };

    // Slightly shrink the outline to render on inside edges
    float x0 = position.x + inset;
    float y0 = position.y + inset;
    float z0 = position.z + inset;
    float x1 = position.x + size - inset;
    float y1 = position.y + size - inset;
    float z1 = position.z + size - inset;

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
