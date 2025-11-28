/**
 * @file mesh_renderer.h
 * @brief High-level mesh rendering system with instance management
 */

#pragma once

#include "mesh/mesh.h"
#include "mesh/mesh_loader.h"
#include <vulkan/vulkan.h>
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
     * @brief Load mesh from GLB/glTF file with textures
     * @param filepath Path to GLB or glTF file
     * @return Mesh ID (use for creating instances)
     */
    uint32_t loadMeshFromGLTF(const std::string& filepath);

    /**
     * @brief Apply automatic bone weights from a rig file to a loaded mesh
     *
     * Calculates bone weights for each vertex based on distance to bone positions.
     * This allows simple models without embedded skinning data to be animated.
     *
     * @param meshId Mesh to apply weights to
     * @param rigPath Path to rig YAML file
     * @param influenceRadius Maximum distance a bone can affect (default 0.5)
     * @return True on success
     */
    bool applySkinningFromRig(uint32_t meshId, const std::string& rigPath,
                              float influenceRadius = 0.5f);

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

    /**
     * @brief Set instance visibility
     * @param instanceId Instance to modify
     * @param visible Whether the instance should be rendered
     */
    void setInstanceVisible(uint32_t instanceId, bool visible);

    /**
     * @brief Check if instance is visible
     * @param instanceId Instance to check
     * @return True if instance is visible
     */
    bool isInstanceVisible(uint32_t instanceId) const;

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

    /**
     * @brief Get texture descriptor set layout for pipeline creation
     */
    VkDescriptorSetLayout getTextureDescriptorSetLayout() const { return m_textureDescriptorSetLayout; }

    /**
     * @brief Get texture descriptor set for binding during rendering
     */
    VkDescriptorSet getTextureDescriptorSet() const { return m_textureDescriptorSet; }

    /**
     * @brief Check if textures are available for rendering
     */
    bool hasTextures() const { return !m_textures.empty(); }

    /**
     * @brief Get material for a mesh
     * @param meshId Mesh ID
     * @return Pointer to material data, or nullptr if not found
     */
    const PBRMaterial* getMeshMaterial(uint32_t meshId) const;

    /**
     * @brief Get mesh bounding box
     * @param meshId Mesh ID
     * @param outMin Output min bounds
     * @param outMax Output max bounds
     * @return True if mesh found, false otherwise
     */
    bool getMeshBounds(uint32_t meshId, glm::vec3& outMin, glm::vec3& outMax) const;

    // ========== Skeletal Animation ==========

    /**
     * @brief Update bone matrices for skeletal animation
     * @param matrices Array of bone transformation matrices
     * @param count Number of bones
     */
    void updateBoneMatrices(const glm::mat4* matrices, int count);

    /**
     * @brief Clear bone matrices (disable skinning)
     */
    void clearBoneMatrices();

private:
    struct MeshData {
        Mesh mesh;
        uint32_t materialId = 0;
        std::vector<uint32_t> instances;  // Instance IDs using this mesh
        VkBuffer instanceBuffer = VK_NULL_HANDLE;
        VkDeviceMemory instanceMemory = VK_NULL_HANDLE;
        bool instanceBufferDirty = false;
        uint32_t visibleInstanceCount = 0;  // Number of visible instances in buffer
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
        bool visible = true;  ///< Whether this instance should be rendered
    };

    // Deferred buffer deletion to avoid destroying in-flight resources
    struct PendingDeletion {
        VkBuffer buffer;
        VkDeviceMemory memory;
        uint64_t frameNumber;  // Frame when deletion was requested
    };

    // GPU texture data
    struct TextureData {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        uint32_t width = 0;
        uint32_t height = 0;
        std::string name;
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

    // Texture storage (indexed by mesh-local texture index)
    std::vector<TextureData> m_textures;
    VkSampler m_textureSampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_textureDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_textureDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_textureDescriptorSet = VK_NULL_HANDLE;
    static constexpr uint32_t MAX_TEXTURES = 64;  // Maximum textures for mesh rendering

    // Deferred deletion queue
    std::vector<PendingDeletion> m_pendingDeletions;
    uint64_t m_frameNumber = 0;
    static constexpr uint64_t FRAMES_TO_KEEP = 3;  // Keep buffers for 3 frames before deletion

    // Bone matrix buffer for skeletal animation
    VkBuffer m_boneBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_boneMemory = VK_NULL_HANDLE;
    void* m_boneMapped = nullptr;
    VkDescriptorPool m_boneDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_boneDescriptorSet = VK_NULL_HANDLE;
    bool m_boneBufferInitialized = false;

    /**
     * @brief Queue buffer for deferred deletion
     */
    void queueBufferDeletion(VkBuffer buffer, VkDeviceMemory memory);

    /**
     * @brief Process pending deletions (call each frame)
     */
    void processPendingDeletions();

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

    /**
     * @brief Initialize texture resources (sampler, descriptor set layout, pool)
     */
    void initializeTextureResources();

    /**
     * @brief Upload a texture image to GPU
     * @param texImage CPU texture data
     * @return Index into m_textures array, or -1 on failure
     */
    int32_t uploadTexture(const TextureImage& texImage);

    /**
     * @brief Update texture descriptor set with current textures
     */
    void updateTextureDescriptorSet();

    /**
     * @brief Clean up all texture resources
     */
    void cleanupTextures();

    /**
     * @brief Initialize bone buffer resources
     */
    void initializeBoneBuffer();

    /**
     * @brief Clean up bone buffer resources
     */
    void cleanupBoneBuffer();
};
