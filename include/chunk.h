/**
 * @file chunk.h
 * @brief Chunk-based terrain storage with procedural generation and mesh optimization
 *
 * Enhanced API documentation by Claude (Anthropic AI Assistant)
 */

#pragma once

#include <vector>
#include <array>
#include <memory>
#include <string>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include "voxelmath.h"
#include "FastNoiseLite.h"

// Forward declaration
class VulkanRenderer;

/**
 * @brief Vertex structure for voxel rendering with position, color, and texture
 *
 * This structure defines the layout of vertex data sent to the GPU.
 * Matches the GLSL vertex shader input layout.
 */
struct Vertex {
    float x, y, z;      ///< Position in world space
    float r, g, b, a;   ///< Color and alpha (fallback if texture not available, alpha for transparency)
    float u, v;         ///< Texture coordinates (atlas UV)

    /**
     * @brief Gets Vulkan binding description for vertex input
     * @return Binding description for vertex buffer
     */
    static inline VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }

    /**
     * @brief Gets Vulkan attribute descriptions for vertex attributes
     * @return Array of attribute descriptions (position, color, texcoord)
     */
    static inline std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};

        // Position attribute (location = 0)
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, x);

        // Color attribute (location = 1) - now includes alpha
        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, r);

        // Texture coordinate attribute (location = 2)
        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(Vertex, u);

        return attributeDescriptions;
    }
};

/**
 * @brief A 32x32x32 section of the voxel world with optimized meshing
 *
 * The Chunk class represents a cubic section of terrain containing 32,768 blocks (32³).
 * It handles:
 * - Procedural terrain generation using FastNoiseLite
 * - Greedy meshing with face culling for optimal vertex count
 * - Vulkan buffer management for rendering
 * - Block storage and modification
 *
 * Coordinate System:
 * - Chunk coordinates (m_x, m_y, m_z) specify the chunk's position in chunk space
 * - Local coordinates (0-31, 0-31, 0-31) specify blocks within the chunk
 * - World coordinates are computed as: chunkCoord * 32 * 0.5 + localCoord * 0.5
 *
 * Meshing Optimization:
 * - Face culling: Hidden faces between solid blocks are not generated
 * - Greedy meshing: Adjacent identical faces are merged into larger quads
 * - Empty chunks: Chunks with no visible geometry have zero vertices
 *
 * Memory Layout:
 * - Block data: 32 KB per chunk (32³ bytes)
 * - Vertex data: Variable, typically 1-10 KB for terrain chunks
 *
 * @note The noise generator is shared across all chunks (static member)
 */
class Chunk {

 static std::unique_ptr<FastNoiseLite> s_noise;  ///< Shared noise generator for terrain (RAII managed)

public:
    // ========== Static Configuration ==========

    /**
     * @brief Initializes the static noise generator with a seed
     *
     * Must be called once before generating any chunks.
     *
     * @param seed Random seed for terrain generation
     */
    static void initNoise(int seed);

    /**
     * @brief Cleans up the static noise generator
     *
     * Call once when shutting down the world.
     */
    static void cleanupNoise();

    static const int WIDTH = 32;   ///< Chunk width in blocks (X axis)
    static const int HEIGHT = 32;  ///< Chunk height in blocks (Y axis)
    static const int DEPTH = 32;   ///< Chunk depth in blocks (Z axis)

    // ========== Construction ==========

    /**
     * @brief Constructs a chunk at the specified chunk coordinates
     *
     * Initializes block storage but does not generate terrain.
     * Call generate() after construction.
     *
     * @param x Chunk X coordinate
     * @param y Chunk Y coordinate
     * @param z Chunk Z coordinate
     */
    Chunk(int x, int y, int z);

    /**
     * @brief Destroys the chunk
     *
     * @warning Vulkan buffers must be destroyed via destroyBuffers() first
     */
    ~Chunk();

    // ========== Terrain Generation ==========

    /**
     * @brief Generates terrain blocks using procedural noise and biome system
     *
     * Fills the chunk with blocks based on biome-specific terrain generation.
     * Does not create mesh - call generateMesh() after all chunks exist.
     *
     * @param biomeMap Biome map for determining terrain type and blocks
     */
    void generate(class BiomeMap* biomeMap);

    /**
     * @brief Generates optimized mesh with face culling
     *
     * Creates vertex and index data for visible block faces only.
     * Performs face culling against adjacent chunks.
     *
     * @param world World instance to query neighboring chunks
     * @note Must be called after all chunks are generated
     */
    void generateMesh(class World* world);

    /**
     * @brief Creates Vulkan vertex and index buffers
     *
     * Uploads mesh data to GPU. Only call if vertexCount > 0.
     *
     * @param renderer Vulkan renderer for buffer creation
     */
    void createVertexBuffer(VulkanRenderer* renderer);

    /**
     * @brief Creates Vulkan vertex and index buffers in batched mode
     *
     * Like createVertexBuffer(), but uses batched buffer copying for better performance.
     * Must be called between renderer->beginBufferCopyBatch() and renderer->submitBufferCopyBatch().
     * After batch submission, call cleanupStagingBuffers() to clean up temporary staging buffers.
     *
     * @param renderer Vulkan renderer for buffer creation
     */
    void createVertexBufferBatched(VulkanRenderer* renderer);

    /**
     * @brief Cleans up staging buffers after batched upload completes
     *
     * Must be called after renderer->submitBufferCopyBatch() completes.
     * Destroys temporary staging buffers created by createVertexBufferBatched().
     *
     * @param renderer Vulkan renderer that created the buffers
     */
    void cleanupStagingBuffers(VulkanRenderer* renderer);

    /**
     * @brief Destroys Vulkan buffers before cleanup
     *
     * Must be called before renderer shutdown.
     *
     * @param renderer Vulkan renderer that created the buffers
     */
    void destroyBuffers(VulkanRenderer* renderer);

    /**
     * @brief Submits draw calls for this chunk
     *
     * Binds vertex/index buffers and issues draw command.
     *
     * @param commandBuffer Vulkan command buffer for recording
     * @param transparent If true, renders transparent geometry; if false, renders opaque geometry
     */
    void render(VkCommandBuffer commandBuffer, bool transparent = false);

    // ========== Terrain Queries ==========

    /**
     * @brief Gets the terrain height at world coordinates (static utility)
     *
     * Uses the noise generator to compute terrain height without needing
     * a chunk instance. Useful for preview or chunk generation.
     *
     * @param worldX World X coordinate
     * @param worldZ World Z coordinate
     * @return Height in blocks (Y coordinate)
     */
    static int getTerrainHeightAt(float worldX, float worldZ);

    // ========== Bounds and Culling ==========

    /**
     * @brief Gets the minimum world-space bounds of this chunk
     * @return Minimum corner position
     */
    glm::vec3 getMin() const { return m_minBounds; }

    /**
     * @brief Gets the maximum world-space bounds of this chunk
     * @return Maximum corner position
     */
    glm::vec3 getMax() const { return m_maxBounds; }

    /**
     * @brief Gets the center position of this chunk in world space
     * @return Center position
     */
    glm::vec3 getCenter() const { return (m_minBounds + m_maxBounds) * 0.5f; }

    /**
     * @brief Gets the number of vertices in this chunk's mesh
     * @return Vertex count (0 if empty/no visible faces)
     */
    uint32_t getVertexCount() const { return m_vertexCount; }

    /**
     * @brief Gets the number of transparent vertices in this chunk's mesh
     * @return Transparent vertex count (0 if no transparent geometry)
     */
    uint32_t getTransparentVertexCount() const { return m_transparentVertexCount; }

    // ========== Block Access ==========

    /**
     * @brief Gets the block ID at local chunk coordinates
     *
     * @param x Local X coordinate (0-31)
     * @param y Local Y coordinate (0-31)
     * @param z Local Z coordinate (0-31)
     * @return Block ID, or -1 if out of bounds
     */
    int getBlock(int x, int y, int z) const;

    /**
     * @brief Sets the block ID at local chunk coordinates
     *
     * Does not regenerate mesh. Call generateMesh() + createVertexBuffer()
     * after modification.
     *
     * @param x Local X coordinate (0-31)
     * @param y Local Y coordinate (0-31)
     * @param z Local Z coordinate (0-31)
     * @param blockID Block ID to set
     */
    void setBlock(int x, int y, int z, int blockID);

    /**
     * @brief Gets the block metadata at local chunk coordinates
     *
     * Metadata is used for water levels, block states, etc.
     *
     * @param x Local X coordinate (0-31)
     * @param y Local Y coordinate (0-31)
     * @param z Local Z coordinate (0-31)
     * @return Metadata value (0-255), or 0 if out of bounds
     */
    uint8_t getBlockMetadata(int x, int y, int z) const;

    /**
     * @brief Sets the block metadata at local chunk coordinates
     *
     * @param x Local X coordinate (0-31)
     * @param y Local Y coordinate (0-31)
     * @param z Local Z coordinate (0-31)
     * @param metadata Metadata value to set (0-255)
     */
    void setBlockMetadata(int x, int y, int z, uint8_t metadata);

    // ========== Chunk Persistence ==========

    /**
     * @brief Saves chunk data to disk in binary format
     *
     * Creates a binary file containing all block and metadata information.
     * File format:
     * - Header (16 bytes): version (4), chunkX (4), chunkY (4), chunkZ (4)
     * - Block data (32 KB): 32x32x32 block IDs
     * - Metadata (32 KB): 32x32x32 metadata bytes
     * Total: ~64 KB per chunk
     *
     * @param worldPath Path to world directory (e.g., "worlds/world_name")
     * @return True if save succeeded, false on error
     */
    bool save(const std::string& worldPath) const;

    /**
     * @brief Loads chunk data from disk
     *
     * Reads binary chunk file and populates block and metadata arrays.
     * Does not regenerate mesh - caller must call generateMesh() after loading.
     *
     * @param worldPath Path to world directory
     * @return True if load succeeded, false if file doesn't exist or is corrupted
     */
    bool load(const std::string& worldPath);

    // ========== Chunk Position ==========

    /**
     * @brief Gets the chunk X coordinate
     * @return Chunk X coordinate in chunk space
     */
    int getChunkX() const { return m_x; }

    /**
     * @brief Gets the chunk Y coordinate
     * @return Chunk Y coordinate in chunk space
     */
    int getChunkY() const { return m_y; }

    /**
     * @brief Gets the chunk Z coordinate
     * @return Chunk Z coordinate in chunk space
     */
    int getChunkZ() const { return m_z; }

    // ========== Visibility State ==========

    /**
     * @brief Gets the visibility state for hysteresis-based culling
     * @return True if chunk was visible last frame
     */
    bool isVisible() const { return m_visible; }

    /**
     * @brief Sets the visibility state
     * @param visible New visibility state
     */
    void setVisible(bool visible) { m_visible = visible; }

private:
    // ========== Greedy Meshing Helpers ==========

    /**
     * @brief Face mask structure for greedy meshing algorithm
     *
     * Tracks which faces should be rendered and whether they've been merged
     * into a larger quad already.
     */
    struct FaceMask {
        int blockID;           ///< Block type (-1 = no face)
        int textureIndex;      ///< Texture index for cube map blocks
        bool merged;           ///< Already included in a merged quad
        bool isTransparent;    ///< Is this a transparent block
        bool isLiquid;         ///< Is this a liquid block
        uint8_t metadata;      ///< Block metadata (water level, etc.)

        FaceMask() : blockID(-1), textureIndex(-1), merged(false),
                     isTransparent(false), isLiquid(false), metadata(0) {}
    };

    /**
     * @brief Builds 2D mask of visible faces for a slice
     * @param mask Output 2D array of face masks
     * @param axis Axis perpendicular to slice (0=X, 1=Y, 2=Z)
     * @param direction Direction along axis (0=negative, 1=positive)
     * @param sliceIndex Index of slice along axis
     * @param world World for neighbor chunk queries
     */
    void buildFaceMask(FaceMask mask[WIDTH][HEIGHT], int axis, int direction,
                       int sliceIndex, class World* world);

    /**
     * @brief Expands rectangle horizontally while blocks match
     * @param mask 2D face mask array
     * @param startX Starting X coordinate
     * @param startY Starting Y coordinate
     * @param blockID Block type to match
     * @param textureIndex Texture index to match
     * @param maskWidth Width of the mask
     * @return Width of expanded rectangle
     */
    int expandRectWidth(const FaceMask mask[WIDTH][HEIGHT], int startX, int startY,
                       int blockID, int textureIndex, int maskWidth) const;

    /**
     * @brief Expands rectangle vertically while entire rows match
     * @param mask 2D face mask array
     * @param startX Starting X coordinate
     * @param startY Starting Y coordinate
     * @param width Width of rectangle (must match for entire row)
     * @param blockID Block type to match
     * @param textureIndex Texture index to match
     * @param maskHeight Height of the mask
     * @return Height of expanded rectangle
     */
    int expandRectHeight(const FaceMask mask[WIDTH][HEIGHT], int startX, int startY,
                        int width, int blockID, int textureIndex, int maskHeight) const;

    /**
     * @brief Generates quad for merged rectangle
     * @param vertices Output vertex array
     * @param indices Output index array
     * @param axis Axis perpendicular to face
     * @param direction Direction along axis
     * @param sliceIndex Slice position along axis
     * @param x Rectangle X coordinate in 2D slice
     * @param y Rectangle Y coordinate in 2D slice
     * @param width Rectangle width
     * @param height Rectangle height
     * @param blockID Block type
     * @param textureIndex Texture index for this face
     * @param isTransparent Whether this is a transparent block
     * @param metadata Block metadata (for water levels)
     */
    void addMergedQuad(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                      int axis, int direction, int sliceIndex,
                      int x, int y, int width, int height,
                      int blockID, int textureIndex, bool isTransparent,
                      uint8_t metadata);

    // ========== Position and Storage ==========
    int m_x, m_y, m_z;                      ///< Chunk coordinates in chunk space
    int m_blocks[WIDTH][HEIGHT][DEPTH];    ///< Block ID storage (32 KB)
    uint8_t m_blockMetadata[WIDTH][HEIGHT][DEPTH]; ///< Block metadata (water levels, etc.) (32 KB)

    // ========== Mesh Data ==========
    std::vector<Vertex> m_vertices;         ///< CPU-side vertex data (opaque)
    std::vector<uint32_t> m_indices;        ///< CPU-side index data (opaque)
    std::vector<Vertex> m_transparentVertices;   ///< CPU-side vertex data (transparent)
    std::vector<uint32_t> m_transparentIndices;  ///< CPU-side index data (transparent)

    // ========== Vulkan Buffers (Opaque) ==========
    VkBuffer m_vertexBuffer;                ///< GPU vertex buffer (opaque)
    VkDeviceMemory m_vertexBufferMemory;    ///< Vertex buffer memory (opaque)
    VkBuffer m_indexBuffer;                 ///< GPU index buffer (opaque)
    VkDeviceMemory m_indexBufferMemory;     ///< Index buffer memory (opaque)
    uint32_t m_vertexCount;                 ///< Number of vertices (opaque)
    uint32_t m_indexCount;                  ///< Number of indices (opaque)

    // ========== Vulkan Buffers (Transparent) ==========
    VkBuffer m_transparentVertexBuffer;           ///< GPU vertex buffer (transparent)
    VkDeviceMemory m_transparentVertexBufferMemory; ///< Vertex buffer memory (transparent)
    VkBuffer m_transparentIndexBuffer;            ///< GPU index buffer (transparent)
    VkDeviceMemory m_transparentIndexBufferMemory; ///< Index buffer memory (transparent)
    uint32_t m_transparentVertexCount;            ///< Number of vertices (transparent)
    uint32_t m_transparentIndexCount;             ///< Number of indices (transparent)

    // ========== Staging Buffers (for batched uploads) ==========
    VkBuffer m_vertexStagingBuffer;               ///< Staging buffer for opaque vertices
    VkDeviceMemory m_vertexStagingBufferMemory;   ///< Staging buffer memory (opaque vertices)
    VkBuffer m_indexStagingBuffer;                ///< Staging buffer for opaque indices
    VkDeviceMemory m_indexStagingBufferMemory;    ///< Staging buffer memory (opaque indices)
    VkBuffer m_transparentVertexStagingBuffer;    ///< Staging buffer for transparent vertices
    VkDeviceMemory m_transparentVertexStagingBufferMemory; ///< Staging buffer memory (transparent vertices)
    VkBuffer m_transparentIndexStagingBuffer;     ///< Staging buffer for transparent indices
    VkDeviceMemory m_transparentIndexStagingBufferMemory;  ///< Staging buffer memory (transparent indices)

    // ========== Culling Data ==========
    glm::vec3 m_minBounds;                  ///< AABB minimum corner (world space)
    glm::vec3 m_maxBounds;                  ///< AABB maximum corner (world space)
    bool m_visible;                         ///< Visibility flag for culling
};