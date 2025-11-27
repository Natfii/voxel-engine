/**
 * @file mesh.h
 * @brief Core mesh data structures for arbitrary 3D model rendering
 *
 * Defines vertex format, mesh structure, and material system for the
 * mesh rendering pipeline (separate from voxel pipeline).
 */

#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <memory>

/**
 * @brief Vertex format for arbitrary mesh rendering
 *
 * Includes all data needed for PBR rendering with normal mapping.
 * Layout designed to match Vulkan vertex input requirements.
 */
struct MeshVertex {
    glm::vec3 position{0.0f};           ///< Local space position
    glm::vec3 normal{0.0f, 1.0f, 0.0f}; ///< Vertex normal (normalized)
    glm::vec2 texCoord{0.0f};           ///< UV texture coordinates
    glm::vec3 tangent{1.0f, 0.0f, 0.0f}; ///< Tangent for normal mapping (PBR)
    glm::vec4 color{1.0f};              ///< Vertex color (RGBA), defaults to white

    /**
     * @brief Get Vulkan binding description for vertex buffer
     * @return Binding description for binding 0 (per-vertex data)
     */
    static VkVertexInputBindingDescription getBindingDescription();

    /**
     * @brief Get Vulkan attribute descriptions for vertex shader
     * @return Array of 5 attribute descriptions (position, normal, uv, tangent, color)
     */
    static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions();
};

/**
 * @brief PBR material properties (metallic-roughness workflow)
 *
 * Supports both constant values and texture maps for each property.
 * Texture indices reference a global texture array managed by the renderer.
 */
struct PBRMaterial {
    glm::vec4 baseColor = glm::vec4(1.0f);     ///< Base color/albedo (RGBA)
    float metallic = 0.0f;                      ///< Metallic factor (0 = dielectric, 1 = metal)
    float roughness = 0.5f;                     ///< Roughness factor (0 = smooth, 1 = rough)
    float emissive = 0.0f;                      ///< Emissive strength (for glowing materials)
    float alphaCutoff = 0.5f;                   ///< Alpha cutoff for transparency masking

    // Texture indices (-1 = no texture, use constant value)
    int32_t albedoTexture = -1;                 ///< Index into texture array
    int32_t normalTexture = -1;                 ///< Normal map index
    int32_t metallicRoughnessTexture = -1;      ///< Metallic (B) + Roughness (G) packed
    int32_t emissiveTexture = -1;               ///< Emissive map index

    /**
     * @brief Create default material (white, non-metallic, medium roughness)
     */
    static PBRMaterial createDefault();

    /**
     * @brief Create debug material with specific color
     * @param color Base color for the material
     */
    static PBRMaterial createDebug(const glm::vec3& color);
};

/**
 * @brief GPU-compatible material uniform buffer
 *
 * Layout matches GLSL std140 alignment rules for uniform buffers.
 * Padding ensures proper GPU memory layout.
 */
struct alignas(16) MaterialUBO {
    alignas(16) glm::vec4 baseColor;
    alignas(4)  float metallic;
    alignas(4)  float roughness;
    alignas(4)  float emissive;
    alignas(4)  float alphaCutoff;
    alignas(4)  int32_t albedoTexIndex;
    alignas(4)  int32_t normalTexIndex;
    alignas(4)  int32_t metallicRoughnessTexIndex;
    alignas(4)  int32_t emissiveTexIndex;

    /**
     * @brief Construct from PBRMaterial
     */
    explicit MaterialUBO(const PBRMaterial& mat);
};

/**
 * @brief Mesh data structure with GPU resources
 *
 * Contains vertex/index data and manages Vulkan buffers for GPU rendering.
 * Each mesh has its own vertex/index buffers (Phase 1 - no mega-buffer yet).
 */
class Mesh {
public:
    std::string name;                           ///< Mesh name (for debugging)
    std::vector<MeshVertex> vertices;           ///< CPU-side vertex data
    std::vector<uint32_t> indices;              ///< CPU-side index data (triangles)
    uint32_t materialIndex = 0;                 ///< Index into material array

    // Bounding box for frustum culling
    glm::vec3 boundsMin = glm::vec3(0.0f);      ///< Axis-aligned bounding box min
    glm::vec3 boundsMax = glm::vec3(0.0f);      ///< Axis-aligned bounding box max

    // GPU resources (managed by renderer)
    VkBuffer vertexBuffer = VK_NULL_HANDLE;     ///< Vulkan vertex buffer
    VkBuffer indexBuffer = VK_NULL_HANDLE;      ///< Vulkan index buffer
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;  ///< Vertex buffer memory
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;   ///< Index buffer memory

    /**
     * @brief Construct empty mesh
     */
    Mesh() = default;

    /**
     * @brief Construct mesh with data
     * @param name Mesh name
     * @param vertices Vertex array
     * @param indices Index array
     * @param materialIndex Material index
     */
    Mesh(const std::string& name,
         const std::vector<MeshVertex>& vertices,
         const std::vector<uint32_t>& indices,
         uint32_t materialIndex = 0);

    /**
     * @brief Calculate bounding box from vertex data
     */
    void calculateBounds();

    /**
     * @brief Calculate tangent vectors for normal mapping
     *
     * Uses vertex positions, normals, and UVs to compute tangent space.
     * Should be called after loading mesh data, before uploading to GPU.
     */
    void calculateTangents();

    /**
     * @brief Check if GPU buffers are allocated
     * @return True if vertex and index buffers exist
     */
    bool hasGPUBuffers() const {
        return vertexBuffer != VK_NULL_HANDLE && indexBuffer != VK_NULL_HANDLE;
    }

    /**
     * @brief Get memory usage in bytes (GPU buffers)
     * @return Total GPU memory used by this mesh
     */
    size_t getGPUMemoryUsage() const;

    /**
     * @brief Get memory usage in bytes (CPU data)
     * @return Total CPU memory used by vertex/index arrays
     */
    size_t getCPUMemoryUsage() const;
};

/**
 * @brief Instance data for mesh rendering
 *
 * Allows rendering multiple copies of the same mesh with different
 * transforms and tint colors using GPU instancing.
 */
struct MeshInstance {
    uint32_t meshIndex;                         ///< Index into mesh array
    glm::mat4 transform;                        ///< Model matrix (position, rotation, scale)
    glm::vec4 tintColor = glm::vec4(1.0f);     ///< Instance-specific color tint
    bool castsShadows = true;                   ///< Enable shadow casting (future)
    bool visible = true;                        ///< Visibility flag

    /**
     * @brief Construct default instance
     */
    MeshInstance() : meshIndex(0), transform(glm::mat4(1.0f)) {}

    /**
     * @brief Construct instance with mesh and transform
     * @param meshIdx Index of mesh to instance
     * @param trans Transformation matrix
     */
    MeshInstance(uint32_t meshIdx, const glm::mat4& trans)
        : meshIndex(meshIdx), transform(trans) {}
};

/**
 * @brief GPU-compatible instance data for vertex shader
 *
 * Uploaded to instance buffer for instanced rendering.
 * Layout: mat4 (4x vec4) + vec4 = 80 bytes per instance
 */
struct InstanceData {
    glm::mat4 transform;        ///< 64 bytes (4x vec4)
    glm::vec4 tintColor;        ///< 16 bytes

    /**
     * @brief Get Vulkan binding description for instance buffer
     * @return Binding description for binding 1 (per-instance data)
     */
    static VkVertexInputBindingDescription getBindingDescription();

    /**
     * @brief Get Vulkan attribute descriptions for instance data
     * @return Array of 5 attributes (mat4 transform + vec4 tint)
     */
    static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions();
};
