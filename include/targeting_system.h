#pragma once

#include "target_info.h"
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <vector>

// Forward declarations
class World;
class VulkanRenderer;

// Unified targeting system that handles crosshair, block outline, and target detection
class TargetingSystem {
public:
    TargetingSystem();
    ~TargetingSystem();

    // Initialization
    void init(VulkanRenderer* renderer);
    void cleanup(VulkanRenderer* renderer);

    // Update targeting (call once per frame)
    void update(World* world, const glm::vec3& playerPos, const glm::vec3& direction);

    // Get current target information
    const TargetInfo& getTarget() const { return m_currentTarget; }

    // Rendering
    void renderCrosshair();  // ImGui rendering
    void renderBlockOutline(VkCommandBuffer commandBuffer);  // Vulkan rendering
    void updateOutlineBuffer(VulkanRenderer* renderer);  // Update outline when target changes

    // Settings
    void setMaxDistance(float dist) { m_maxDistance = dist; }
    void setEnabled(bool enabled) { m_enabled = enabled; }
    void setCrosshairVisible(bool visible) { m_crosshairVisible = visible; }
    void setOutlineVisible(bool visible) { m_outlineVisible = visible; }

    bool isEnabled() const { return m_enabled; }

private:
    // Target state
    TargetInfo m_currentTarget;
    bool m_enabled;
    float m_maxDistance;

    // Crosshair settings
    bool m_crosshairVisible;
    float m_crosshairSize;
    float m_crosshairThickness;
    float m_crosshairGap;

    // Block outline (Vulkan)
    bool m_outlineVisible;
    VkBuffer m_outlineVertexBuffer;
    VkDeviceMemory m_outlineVertexBufferMemory;
    uint32_t m_outlineVertexCount;

    // Helper methods
    void updateTargetInfo(World* world);
    std::vector<float> createOutlineVertices(const glm::vec3& position);
};
