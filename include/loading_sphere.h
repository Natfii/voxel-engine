/**
 * @file loading_sphere.h
 * @brief Spinning 3D sphere for loading screen preview
 *
 * Renders a slowly rotating 3D sphere with the map preview texture
 * during world generation. Uses the existing voxel graphics pipeline.
 *
 * Created: 2025-11-25
 */

#pragma once

#include "chunk.h"  // For Vertex struct
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <chrono>

// Forward declarations
class VulkanRenderer;
class MapPreview;

/**
 * @brief 3D spinning sphere for loading screen
 *
 * Creates a UV sphere mesh using the voxel Vertex format and renders
 * it with time-based rotation during loading. Shows the map preview
 * texture wrapped around a globe.
 */
class LoadingSphere {
public:
    LoadingSphere();
    ~LoadingSphere();

    /**
     * @brief Initialize the sphere mesh and GPU resources
     * @param renderer Vulkan renderer for GPU resource creation
     * @return True if initialization succeeded
     */
    bool initialize(VulkanRenderer* renderer);

    /**
     * @brief Cleanup GPU resources
     */
    void cleanup();

    /**
     * @brief Set the map preview to use as texture
     * @param mapPreview Pointer to map preview (can be null for gradient fallback)
     */
    void setMapPreview(MapPreview* mapPreview);

    /**
     * @brief Render the spinning sphere
     *
     * Call this during loading screen, between beginFrame() and ImGui rendering.
     * Updates uniform buffer with sphere-specific MVP and renders.
     */
    void render();

    /**
     * @brief Set rotation speed
     * @param degreesPerSecond Rotation speed around Y axis (default: 30)
     */
    void setRotationSpeed(float degreesPerSecond) { m_rotationSpeed = degreesPerSecond; }

    /**
     * @brief Check if sphere is ready for rendering
     */
    bool isReady() const { return m_initialized; }

    /**
     * @brief Reset the rotation timer (call when starting a new load)
     */
    void resetTimer() { m_startTime = std::chrono::steady_clock::now(); }

private:
    /**
     * @brief Generate UV sphere vertex and index data
     */
    void generateSphereMesh();

    /**
     * @brief Create and upload GPU buffers
     */
    bool createBuffers();

    VulkanRenderer* m_renderer = nullptr;
    MapPreview* m_mapPreview = nullptr;
    bool m_initialized = false;
    bool m_hasMapTexture = false;
    bool m_triedCreatingDescriptor = false;  // Reset when setMapPreview() called

    // Mesh data
    std::vector<Vertex> m_vertices;
    std::vector<uint32_t> m_indices;

    // GPU resources
    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexMemory = VK_NULL_HANDLE;
    VkBuffer m_indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_indexMemory = VK_NULL_HANDLE;

    // Descriptor set for map preview texture
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;

    // Animation
    std::chrono::steady_clock::time_point m_startTime;
    float m_rotationSpeed = 30.0f;  // Degrees per second

    // Sphere parameters
    static constexpr float SPHERE_RADIUS = 0.5f;
    static constexpr int SPHERE_SEGMENTS = 32;  // lat/lon segments
};
