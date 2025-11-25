/**
 * @file mesh_renderer.h
 * @brief High-level mesh rendering system with instance management
 */

#pragma once

#include "mesh/mesh.h"
#include "mesh/mesh_loader.h"
#include <vector>
#include <unordered_map>
#include <memory>

// Forward declaration
class VulkanRenderer;

/**
 * @brief Manages all mesh rendering including meshes, materials, and instances
 *
 * Provides a high-level API for:
 * - Loading and managing meshes
 * - Creating and managing materials
 * - Instancing meshes with transforms
 * - Rendering all meshes efficiently
 */
class MeshRenderer {
public:
    /**
     * @brief Construct mesh renderer
     * @param renderer Pointer to Vulkan renderer
     */
    explicit MeshRenderer(VulkanRenderer* renderer);

    /**
     * @brief Destructor - cleans up all GPU resources
     */
    ~MeshRenderer();

    // Prevent copying
    MeshRenderer(const MeshRenderer&) = delete;
    MeshRenderer& operator=(const MeshRenderer&) = delete;

    // ========== Mesh Management ==========

    /**
     * @brief Load mesh from OBJ file
     * @param filepath Path to OBJ file
     * @return Mesh ID (use for creating instances)
     */
    uint32_t loadMeshFromFile(const std::string& filepath);

    /**
     * @brief Create procedural mesh
     * @param mesh Mesh data
     * @return Mesh ID
     */
    uint32_t createMesh(const Mesh& mesh);

    /**
     * @brief Remove mesh and all its instances
     * @param meshId Mesh ID to remove
     */
    void removeMesh(uint32_t meshId);

    // ========== Material Management ==========

    /**
     * @brief Create material
     * @param material PBR material properties
     * @return Material ID
     */
    uint32_t createMaterial(const PBRMaterial& material);

    /**
     * @brief Update material properties
     * @param materialId Material to update
     * @param material New material properties
     */
    void updateMaterial(uint32_t materialId, const PBRMaterial& material);

    /**
     * @brief Set mesh material
     * @param meshId Mesh to modify
     * @param materialId Material to assign
     */
    void setMeshMaterial(uint32_t meshId, uint32_t materialId);

    // ========== Instance Management ==========

    /**
     * @brief Create mesh instance
     * @param meshId Mesh to instance
     * @param transform Model matrix (position, rotation, scale)
     * @param tintColor Optional color tint
     * @return Instance ID
     */
    uint32_t createInstance(uint32_t meshId, const glm::mat4& transform,
                           const glm::vec4& tintColor = glm::vec4(1.0f));

    /**
     * @brief Update instance transform
     * @param instanceId Instance to update
     * @param transform New transform matrix
     */
    void updateInstanceTransform(uint32_t instanceId, const glm::mat4& transform);

    /**
     * @brief Update instance tint color
     * @param instanceId Instance to update
     * @param tintColor New tint color
     */
    void updateInstanceColor(uint32_t instanceId, const glm::vec4& tintColor);

    /**
     * @brief Remove instance
     * @param instanceId Instance to remove
     */
    void removeInstance(uint32_t instanceId);

    // ========== Rendering ==========

    /**
     * @brief Render all mesh instances
     *
     * Call this during frame rendering, after binding camera descriptor set.
     * Renders all opaque meshes with instancing.
     *
     * @param cmd Command buffer to record into
     */
    void render(VkCommandBuffer cmd);

    /**
     * @brief Get number of meshes
     */
    size_t getMeshCount() const { return m_meshes.size(); }

    /**
     * @brief Get number of instances
     */
    size_t getInstanceCount() const { return m_instanceCount; }

    /**
     * @brief Get GPU memory usage in bytes
     */
    size_t getGPUMemoryUsage() const;

private:
    struct MeshData {
        Mesh mesh;
        uint32_t materialId = 0;
        std::vector<uint32_t> instances;  // Instance IDs using this mesh
        VkBuffer instanceBuffer = VK_NULL_HANDLE;
        VkDeviceMemory instanceMemory = VK_NULL_HANDLE;
        bool instanceBufferDirty = false;
    };

    struct MaterialData {
        PBRMaterial material;
        VkBuffer uniformBuffer = VK_NULL_HANDLE;
        VkDeviceMemory uniformMemory = VK_NULL_HANDLE;
        void* uniformMapped = nullptr;
    };

    struct InstanceInfo {
        uint32_t meshId;
        InstanceData data;
    };

    VulkanRenderer* m_renderer;

    // Mesh storage
    std::unordered_map<uint32_t, MeshData> m_meshes;
    uint32_t m_nextMeshId = 1;

    // Material storage
    std::unordered_map<uint32_t, MaterialData> m_materials;
    uint32_t m_nextMaterialId = 1;
    uint32_t m_defaultMaterialId = 0;

    // Instance storage
    std::unordered_map<uint32_t, InstanceInfo> m_instances;
    uint32_t m_nextInstanceId = 1;
    size_t m_instanceCount = 0;

    /**
     * @brief Upload mesh geometry to GPU
     */
    void uploadMesh(MeshData& meshData);

    /**
     * @brief Update instance buffer for a mesh
     */
    void updateInstanceBuffer(MeshData& meshData);

    /**
     * @brief Create or update material uniform buffer
     */
    void uploadMaterial(MaterialData& materialData);
};
