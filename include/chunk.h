/**
 * @file chunk.h
 * @brief Chunk-based terrain storage with procedural generation and mesh optimization
 */

#pragma once

#include <vector>
#include <array>
#include <memory>
#include <string>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include "voxelmath.h"
#include "FastNoiseLite.h"
#include "block_light.h"

// Forward declaration
class VulkanRenderer;

/**
 * @brief Chunk lifecycle state machine for explicit state tracking
 *
 * ARCHITECTURE IMPROVEMENT (2025-11-25): Replaces scattered boolean flags
 * with explicit state machine for clearer debugging and state validation.
 *
 * State transitions:
 *   UNLOADED -> LOADING (worker thread picks up chunk)
 *   LOADING -> GENERATED (terrain complete, awaiting decoration)
 *   GENERATED -> DECORATING (decoration in progress)
 *   DECORATING -> AWAITING_MESH (decoration complete)
 *   AWAITING_MESH -> MESHING (mesh worker picks up chunk)
 *   MESHING -> AWAITING_UPLOAD (mesh complete)
 *   AWAITING_UPLOAD -> UPLOADING (GPU upload started)
 *   UPLOADING -> ACTIVE (fully renderable)
 *   ACTIVE -> UNLOADING (chunk being removed)
 *   UNLOADING -> UNLOADED (returned to pool)
 *
 * @note Use atomic operations for thread-safe state transitions
 */
enum class ChunkState : uint8_t {
    UNLOADED,           ///< Not in memory (in pool or not created)
    LOADING,            ///< Terrain generation in progress (worker thread)
    GENERATED,          ///< Terrain generated, awaiting decoration
    DECORATING,         ///< Tree/structure decoration in progress
    AWAITING_MESH,      ///< Ready for mesh generation
    MESHING,            ///< Mesh generation in progress
    AWAITING_UPLOAD,    ///< Mesh ready, waiting for GPU upload
    UPLOADING,          ///< GPU upload in progress
    ACTIVE,             ///< Fully loaded and renderable
    UNLOADING           ///< Being unloaded (saving, cleanup)
};

/**
 * @brief Converts ChunkState to human-readable string for debugging
 * @param state The chunk state
 * @return String representation
 */
inline const char* chunkStateToString(ChunkState state) {
    switch (state) {
        case ChunkState::UNLOADED:        return "UNLOADED";
        case ChunkState::LOADING:         return "LOADING";
        case ChunkState::GENERATED:       return "GENERATED";
        case ChunkState::DECORATING:      return "DECORATING";
        case ChunkState::AWAITING_MESH:   return "AWAITING_MESH";
        case ChunkState::MESHING:         return "MESHING";
        case ChunkState::AWAITING_UPLOAD: return "AWAITING_UPLOAD";
        case ChunkState::UPLOADING:       return "UPLOADING";
        case ChunkState::ACTIVE:          return "ACTIVE";
        case ChunkState::UNLOADING:       return "UNLOADING";
        default:                          return "UNKNOWN";
    }
}

/**
 * @brief Compressed vertex structure for voxel rendering (12 bytes vs 48 bytes)
 *
 * Based on Vercidium's voxel optimization technique:
 * https://vercidium.com/blog/voxel-world-optimisations/
 *
 * Uses world coordinates (signed 16-bit) to support mega-buffer indirect drawing.
 *
 * Memory layout (12 bytes total, using universally supported Vulkan formats):
 *   posXY (uint32_t = 4 bytes): Packed position X and Y
 *     Bits 0-15:  Position X (int16, range ±32767)
 *     Bits 16-31: Position Y (int16, range ±32767)
 *   posZAtlas (uint32_t = 4 bytes): Packed position Z and atlas coords
 *     Bits 0-15:  Position Z (int16, range ±32767)
 *     Bits 16-23: Atlas X cell (0-255)
 *     Bits 24-31: Atlas Y cell (0-255)
 *   packedB (uint32_t = 4 bytes): All other vertex data
 *     Bits 0-2:   Normal direction (0-5: +X, -X, +Y, -Y, +Z, -Z)
 *     Bits 3-7:   Quad width for UV tiling (0-31)
 *     Bits 8-12:  Quad height for UV tiling (0-31)
 *     Bits 13-14: Corner index (0-3)
 *     Bits 15-18: Sky light level (0-15)
 *     Bits 19-22: Block light level (0-15)
 *     Bits 23-26: Ambient occlusion (0-15)
 *     Bits 27-28: Color tint index (0-3)
 *     Bits 29-31: Reserved
 *
 * Memory savings: 48 bytes -> 12 bytes = 4x reduction!
 */
struct CompressedVertex {
    uint32_t posXY;      ///< Position X (low 16) + Position Y (high 16)
    uint32_t posZAtlas;  ///< Position Z (low 16) + Atlas X (bits 16-23) + Atlas Y (bits 24-31)
    uint32_t packedB;    ///< Normal + QuadSize + Corner + Lighting + Tint

    // Normal direction constants
    static constexpr uint8_t NORMAL_POS_X = 0;
    static constexpr uint8_t NORMAL_NEG_X = 1;
    static constexpr uint8_t NORMAL_POS_Y = 2;
    static constexpr uint8_t NORMAL_NEG_Y = 3;
    static constexpr uint8_t NORMAL_POS_Z = 4;
    static constexpr uint8_t NORMAL_NEG_Z = 5;

    // Corner indices for UV calculation (matches cubeUVs order)
    // Side faces (V-flipped): vertex 0=CORNER_HEIGHT, 1=CORNER_BOTH, 2=CORNER_WIDTH, 3=CORNER_ORIGIN
    // Top/bottom faces: vertex 0=CORNER_ORIGIN, 1=CORNER_WIDTH, 2=CORNER_BOTH, 3=CORNER_HEIGHT
    static constexpr uint8_t CORNER_ORIGIN = 0;      // UV (0, 0)
    static constexpr uint8_t CORNER_WIDTH = 1;       // UV (quadWidth, 0)
    static constexpr uint8_t CORNER_HEIGHT = 2;      // UV (0, quadHeight)
    static constexpr uint8_t CORNER_BOTH = 3;        // UV (quadWidth, quadHeight)

    // Color tint palette indices (shader will lookup actual colors)
    static constexpr uint8_t TINT_WHITE = 0;       // Default (1,1,1,1)
    static constexpr uint8_t TINT_WATER = 1;       // Water blue tint
    static constexpr uint8_t TINT_FOLIAGE = 2;     // Foliage green
    static constexpr uint8_t TINT_GRASS = 3;       // Grass/leaves green

    /**
     * @brief Pack vertex data into compressed format
     * @param worldX World X position (stored as int16)
     * @param worldY World Y position (stored as int16)
     * @param worldZ World Z position (stored as int16)
     * @param normalIndex Normal direction (0-5)
     * @param quadWidth Width of quad for UV tiling (0-31)
     * @param quadHeight Height of quad for UV tiling (0-31)
     * @param atlasX Texture atlas X cell (0-255)
     * @param atlasY Texture atlas Y cell (0-255)
     * @param cornerIndex Which corner of the quad (0-3)
     * @param skyLight Sky light level (0-15)
     * @param blockLight Block light level (0-15)
     * @param ao Ambient occlusion (0-15)
     * @param colorTint Color tint index (0-3)
     */
    static inline CompressedVertex pack(
        float worldX, float worldY, float worldZ,
        uint8_t normalIndex,
        uint8_t quadWidth, uint8_t quadHeight,
        uint8_t atlasX, uint8_t atlasY,
        uint8_t cornerIndex,
        uint8_t skyLight, uint8_t blockLight, uint8_t ao,
        uint8_t colorTint
    ) {
        CompressedVertex cv;

        // Store world position as signed 16-bit integers packed into uint32s
        int16_t px = static_cast<int16_t>(std::clamp(worldX, -32767.0f, 32767.0f));
        int16_t py = static_cast<int16_t>(std::clamp(worldY, -32767.0f, 32767.0f));
        int16_t pz = static_cast<int16_t>(std::clamp(worldZ, -32767.0f, 32767.0f));

        // Pack posX and posY into posXY (reinterpret int16 as uint16 for bit packing)
        cv.posXY = (static_cast<uint32_t>(static_cast<uint16_t>(px)))
                 | (static_cast<uint32_t>(static_cast<uint16_t>(py)) << 16);

        // Pack posZ and atlas coords into posZAtlas
        cv.posZAtlas = (static_cast<uint32_t>(static_cast<uint16_t>(pz)))
                     | (static_cast<uint32_t>(atlasX) << 16)
                     | (static_cast<uint32_t>(atlasY) << 24);

        // Pack remaining data
        cv.packedB = (static_cast<uint32_t>(normalIndex & 0x7))           // bits 0-2
                   | (static_cast<uint32_t>(quadWidth & 0x1F) << 3)       // bits 3-7
                   | (static_cast<uint32_t>(quadHeight & 0x1F) << 8)      // bits 8-12
                   | (static_cast<uint32_t>(cornerIndex & 0x3) << 13)     // bits 13-14
                   | (static_cast<uint32_t>(skyLight & 0xF) << 15)        // bits 15-18
                   | (static_cast<uint32_t>(blockLight & 0xF) << 19)      // bits 19-22
                   | (static_cast<uint32_t>(ao & 0xF) << 23)              // bits 23-26
                   | (static_cast<uint32_t>(colorTint & 0x3) << 27);      // bits 27-28

        return cv;
    }

    /**
     * @brief Gets Vulkan binding description for compressed vertex
     */
    static inline VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(CompressedVertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }

    /**
     * @brief Gets Vulkan attribute descriptions for compressed vertex
     * @return Array of 3 attribute descriptions (posXY, posZAtlas, packedB)
     */
    static inline std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};

        // posXY (location = 0) - packed posX and posY as uint32
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32_UINT;
        attributeDescriptions[0].offset = offsetof(CompressedVertex, posXY);

        // posZAtlas (location = 1) - packed posZ and atlas coords as uint32
        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32_UINT;
        attributeDescriptions[1].offset = offsetof(CompressedVertex, posZAtlas);

        // packedB (location = 2) - everything else as uint32
        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32_UINT;
        attributeDescriptions[2].offset = offsetof(CompressedVertex, packedB);

        return attributeDescriptions;
    }
};

/**
 * @brief Legacy vertex structure for voxel rendering (kept for compatibility)
 *
 * This structure defines the layout of vertex data sent to the GPU.
 * Matches the GLSL vertex shader input layout.
 * @deprecated Use CompressedVertex for new code - 6x smaller
 */
struct Vertex {
    float x, y, z;      ///< Position in world space
    float r, g, b, a;   ///< Color and alpha (fallback if texture not available, alpha for transparency)
    float u, v;         ///< Texture coordinates (atlas UV)
    float skyLight;     ///< Sky light level 0.0-1.0 (affected by sun/moon position)
    float blockLight;   ///< Block light level 0.0-1.0 (torches, lava - constant)
    float ao;           ///< Ambient occlusion 0.0-1.0 (darkens corners where blocks meet)

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
     * @return Array of attribute descriptions (position, color, texcoord, skyLight, blockLight, ao)
     */
    static inline std::array<VkVertexInputAttributeDescription, 6> getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 6> attributeDescriptions{};

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

        // Sky light attribute (location = 3)
        attributeDescriptions[3].binding = 0;
        attributeDescriptions[3].location = 3;
        attributeDescriptions[3].format = VK_FORMAT_R32_SFLOAT;
        attributeDescriptions[3].offset = offsetof(Vertex, skyLight);

        // Block light attribute (location = 4)
        attributeDescriptions[4].binding = 0;
        attributeDescriptions[4].location = 4;
        attributeDescriptions[4].format = VK_FORMAT_R32_SFLOAT;
        attributeDescriptions[4].offset = offsetof(Vertex, blockLight);

        // Ambient occlusion attribute (location = 5)
        attributeDescriptions[5].binding = 0;
        attributeDescriptions[5].location = 5;
        attributeDescriptions[5].format = VK_FORMAT_R32_SFLOAT;
        attributeDescriptions[5].offset = offsetof(Vertex, ao);

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
    // VulkanRenderer needs access to staging buffers for async upload management
    friend class VulkanRenderer;

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
     * @brief Generates optimized mesh with face culling and LOD support
     *
     * Creates vertex and index data for visible block faces only.
     * Performs face culling against adjacent chunks.
     * PERFORMANCE (2025-11-24): LOD reduces triangle count for distant chunks
     *
     * @param world World instance to query neighboring chunks
     * @param callerHoldsLock If true, caller already holds world's chunk map lock (prevents deadlock)
     * @param lodLevel Level of detail (0=full, 1=half, 2=quarter resolution)
     * @note Must be called after all chunks are generated
     */
    void generateMesh(class World* world, bool callerHoldsLock = false, int lodLevel = 0);

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

    /**
     * @brief Gets the number of indices in this chunk's opaque mesh
     * @return Index count (0 if empty/no visible faces)
     */
    uint32_t getIndexCount() const { return m_indexCount; }

    /**
     * @brief Gets the number of indices in this chunk's transparent mesh
     * @return Transparent index count (0 if no transparent geometry)
     */
    uint32_t getTransparentIndexCount() const { return m_transparentIndexCount; }

    // ========== Indirect Drawing Getters (GPU Optimization) ==========

    /**
     * @brief Gets the vertex offset in the mega-buffer (for indirect drawing)
     * @return Byte offset in opaque vertex mega-buffer
     */
    VkDeviceSize getMegaBufferVertexOffset() const { return m_megaBufferVertexOffset; }

    /**
     * @brief Gets the index offset in the mega-buffer (for indirect drawing)
     * @return Byte offset in opaque index mega-buffer
     */
    VkDeviceSize getMegaBufferIndexOffset() const { return m_megaBufferIndexOffset; }

    /**
     * @brief Gets the base vertex for indexed drawing (for indirect drawing)
     * @return Base vertex offset
     */
    uint32_t getMegaBufferBaseVertex() const { return m_megaBufferBaseVertex; }

    /**
     * @brief Gets the transparent vertex offset in the mega-buffer (for indirect drawing)
     * @return Byte offset in transparent vertex mega-buffer
     */
    VkDeviceSize getMegaBufferTransparentVertexOffset() const { return m_megaBufferTransparentVertexOffset; }

    /**
     * @brief Gets the transparent index offset in the mega-buffer (for indirect drawing)
     * @return Byte offset in transparent index mega-buffer
     */
    VkDeviceSize getMegaBufferTransparentIndexOffset() const { return m_megaBufferTransparentIndexOffset; }

    /**
     * @brief Gets the transparent base vertex for indexed drawing (for indirect drawing)
     * @return Transparent base vertex offset
     */
    uint32_t getMegaBufferTransparentBaseVertex() const { return m_megaBufferTransparentBaseVertex; }

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

    // ========== Lighting ==========

    /**
     * @brief Gets the sky light level at local chunk coordinates
     *
     * @param x Local X coordinate (0-31)
     * @param y Local Y coordinate (0-31)
     * @param z Local Z coordinate (0-31)
     * @return Sky light level (0-15), or 0 if out of bounds
     */
    uint8_t getSkyLight(int x, int y, int z) const;

    /**
     * @brief Gets the block light level at local chunk coordinates
     *
     * @param x Local X coordinate (0-31)
     * @param y Local Y coordinate (0-31)
     * @param z Local Z coordinate (0-31)
     * @return Block light level (0-15), or 0 if out of bounds
     */
    uint8_t getBlockLight(int x, int y, int z) const;

    /**
     * @brief Sets the sky light level at local chunk coordinates
     *
     * @param x Local X coordinate (0-31)
     * @param y Local Y coordinate (0-31)
     * @param z Local Z coordinate (0-31)
     * @param value Sky light level (0-15)
     */
    void setSkyLight(int x, int y, int z, uint8_t value);

    /**
     * @brief Gets interpolated sky light (smooth time-based transitions)
     *
     * @param x Local X coordinate (0-31)
     * @param y Local Y coordinate (0-31)
     * @param z Local Z coordinate (0-31)
     * @return Interpolated sky light level (0.0-15.0)
     */
    float getInterpolatedSkyLight(int x, int y, int z) const;

    /**
     * @brief Gets interpolated block light (smooth time-based transitions)
     *
     * @param x Local X coordinate (0-31)
     * @param y Local Y coordinate (0-31)
     * @param z Local Z coordinate (0-31)
     * @return Interpolated block light level (0.0-15.0)
     */
    float getInterpolatedBlockLight(int x, int y, int z) const;

    /**
     * @brief Updates interpolated lighting values toward target values
     *
     * Smoothly transitions lighting over time for natural-looking changes.
     *
     * @param deltaTime Time since last frame in seconds
     * @param speed Interpolation speed multiplier (higher = faster transitions)
     */
    void updateInterpolatedLighting(float deltaTime, float speed = 5.0f);

    /**
     * @brief Initializes interpolated lighting to match current target values
     *
     * Call this after initial lighting calculation to avoid fade-in effect.
     * Sets interpolated values = target values immediately (no transition).
     */
    void initializeInterpolatedLighting();

    /**
     * @brief Sets the block light level at local chunk coordinates
     *
     * @param x Local X coordinate (0-31)
     * @param y Local Y coordinate (0-31)
     * @param z Local Z coordinate (0-31)
     * @param value Block light level (0-15)
     */
    void setBlockLight(int x, int y, int z, uint8_t value);

    /**
     * @brief Marks this chunk's lighting as dirty (needs mesh regeneration)
     */
    void markLightingDirty() { m_lightingDirty = true; }

    /**
     * @brief Checks if this chunk needs decoration (freshly generated)
     * @return True if chunk needs tree/structure placement
     */
    bool needsDecoration() const { return m_needsDecoration; }

    /**
     * @brief Sets whether this chunk needs decoration
     * @param needs True for freshly generated chunks, false for loaded chunks
     */
    void setNeedsDecoration(bool needs) { m_needsDecoration = needs; }

    /**
     * @brief Checks if chunk has pre-initialized lighting data (Version 3 chunks)
     * @return True if chunk loaded with lighting, false if needs lighting initialization
     */
    bool hasLightingData() const { return m_hasLightingData; }

    /**
     * @brief Sets whether chunk has pre-initialized lighting data
     * @param hasData True for Version 3 loaded chunks, false for fresh/Version 1-2 chunks
     */
    void setHasLightingData(bool hasData) { m_hasLightingData = hasData; }

    /**
     * @brief Checks if chunk terrain generation is complete (Stage 1 of multi-stage generation)
     *
     * MULTI-STAGE GENERATION (Minecraft-style):
     * - Stage 1: Terrain generation (blocks, heightmap) - marks terrainReady=true
     * - Stage 2: Decoration (trees, structures) - requires all 4 neighbors to have terrainReady=true
     *
     * This prevents deadlock where chunks wait for neighbors that never finish terrain generation.
     *
     * @return True if terrain generation is complete (safe for neighbors to decorate)
     */
    bool isTerrainReady() const { return m_terrainReady; }

    /**
     * @brief Marks chunk terrain generation as complete (Stage 1)
     * @param ready True after terrain generation, false for fresh chunks
     */
    void setTerrainReady(bool ready) { m_terrainReady = ready; }

    // ========== Heightmap (Fast Sky Light) ==========

    /**
     * @brief Gets the height of the highest solid block in a column
     *
     * Used for fast sky light calculation: blocks above heightmap get full sunlight.
     *
     * @param x Local X coordinate (0-31)
     * @param z Local Z coordinate (0-31)
     * @return Y coordinate of highest solid block, or -1 if column is all air
     */
    int16_t getHeightAt(int x, int z) const {
        if (x < 0 || x >= WIDTH || z < 0 || z >= DEPTH) return -1;
        return m_heightMap[x * DEPTH + z];
    }

    /**
     * @brief Updates heightmap for a column after block change
     *
     * Call this whenever a block is placed or broken to keep heightmap accurate.
     *
     * @param x Local X coordinate (0-31)
     * @param z Local Z coordinate (0-31)
     */
    void updateHeightAt(int x, int z);

    /**
     * @brief Rebuilds entire heightmap by scanning all blocks
     *
     * Call this once after chunk generation to initialize heightmap.
     * Scans from top to bottom to find highest solid block in each column.
     */
    void rebuildHeightMap();

    /**
     * @brief Calculates sky light using heightmap (O(1) lookup, no BFS!)
     *
     * PERFORMANCE: Replaces expensive BFS propagation with instant calculation.
     * Blocks above heightmap = full sunlight (15), blocks below = dark (0).
     *
     * @param x Local X coordinate (0-31)
     * @param y Local Y coordinate (0-31)
     * @param z Local Z coordinate (0-31)
     * @return Sky light level (0-15)
     */
    uint8_t calculateSkyLightFromHeightmap(int x, int y, int z) const {
        if (x < 0 || x >= WIDTH || z < 0 || z >= DEPTH) return 0;
        if (y < 0 || y >= HEIGHT) return 0;

        int16_t heightAtColumn = m_heightMap[x * DEPTH + z];

        // Blocks above highest solid block get full sunlight
        // This is classic Minecraft behavior (caves get sunlight, but fast!)
        if (y > heightAtColumn) {
            return 15;  // Full sunlight
        } else {
            return 0;   // No sunlight (underground/inside blocks)
        }
    }

    /**
     * @brief Checks if chunk lighting is dirty
     * @return True if lighting needs mesh regeneration
     */
    bool isLightingDirty() const { return m_lightingDirty; }

    /**
     * @brief Clears the lighting dirty flag
     */
    void clearLightingDirty() { m_lightingDirty = false; }

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

    /**
     * @brief Checks if chunk is fully occluded by neighbors
     *
     * OCCLUSION CULLING: If a chunk is completely surrounded by solid opaque chunks,
     * it's invisible and can skip mesh generation and rendering entirely.
     * Huge performance win for underground chunks!
     *
     * @param world World instance to query neighbors
     * @param callerHoldsLock True if caller already holds World::m_chunkMapMutex (prevents deadlock)
     * @return True if chunk is fully occluded (all 6 neighbors are solid)
     */
    bool isFullyOccluded(class World* world, bool callerHoldsLock = false) const;

    // ========== Chunk Pooling ==========

    /**
     * @brief Resets chunk for reuse from pool
     *
     * Clears all blocks to air, resets position, and prepares chunk for
     * reuse without deallocating memory. Much faster than new/delete.
     *
     * @param x New chunk X coordinate
     * @param y New chunk Y coordinate
     * @param z New chunk Z coordinate
     * @note Does NOT destroy Vulkan buffers - caller must do that first
     */
    void reset(int x, int y, int z);

    /**
     * @brief Checks if chunk is completely empty (all air)
     * @return True if all blocks are air, false otherwise
     */
    bool isEmpty() const;

    /**
     * @brief Deallocates interpolated lighting to save memory
     * Called when chunk is unloaded to cache (saves 256KB per chunk)
     */
    void deallocateInterpolatedLighting();

    // ========== Chunk State Machine ==========

    /**
     * @brief Gets the current chunk state
     * @return Current state (thread-safe atomic read)
     */
    ChunkState getState() const { return m_state.load(std::memory_order_acquire); }

    /**
     * @brief Sets the chunk state directly (use transitionTo for validation)
     * @param state New state
     */
    void setState(ChunkState state) { m_state.store(state, std::memory_order_release); }

    /**
     * @brief Attempts to transition to a new state with validation
     *
     * Validates that the transition is legal according to the state machine.
     * Invalid transitions are logged and rejected.
     *
     * @param newState Target state
     * @return True if transition succeeded, false if invalid
     */
    bool transitionTo(ChunkState newState);

    /**
     * @brief Attempts atomic compare-and-swap state transition
     *
     * Thread-safe transition that only succeeds if current state matches expected.
     * Useful for worker threads competing to claim chunks.
     *
     * @param expectedState Expected current state
     * @param newState Target state
     * @return True if transition succeeded (state was expectedState and is now newState)
     */
    bool tryTransition(ChunkState expectedState, ChunkState newState);

    /**
     * @brief Checks if chunk is in a renderable state
     * @return True if state is ACTIVE
     */
    bool isRenderable() const { return getState() == ChunkState::ACTIVE; }

    /**
     * @brief Checks if chunk needs mesh generation
     * @return True if state is AWAITING_MESH
     */
    bool needsMesh() const { return getState() == ChunkState::AWAITING_MESH; }

    /**
     * @brief Checks if chunk needs GPU upload
     * @return True if state is AWAITING_UPLOAD
     */
    bool needsUpload() const { return getState() == ChunkState::AWAITING_UPLOAD; }

private:
    // ========== State Machine ==========
    std::atomic<ChunkState> m_state{ChunkState::UNLOADED}; ///< Current chunk lifecycle state (thread-safe)

    // ========== Position and Storage ==========
    int m_x, m_y, m_z;                      ///< Chunk coordinates in chunk space
    int m_blocks[WIDTH][HEIGHT][DEPTH];    ///< Block ID storage (32 KB)
    uint8_t m_blockMetadata[WIDTH][HEIGHT][DEPTH]; ///< Block metadata (water levels, etc.) (32 KB)
    mutable std::mutex m_blockDataMutex;    ///< THREAD SAFETY: Protects m_blocks and m_blockMetadata for parallel decoration
    std::array<BlockLight, WIDTH * HEIGHT * DEPTH> m_lightData; ///< Light data (sky + block light, 32 KB)
    bool m_lightingDirty;                   ///< True if lighting changed (needs mesh regen)
    bool m_needsDecoration;                 ///< True if chunk is freshly generated and needs decoration
    bool m_hasLightingData;                 ///< True if chunk loaded with lighting data (Version 3), prevents re-initialization
    bool m_terrainReady;                    ///< STAGE 1 COMPLETE: Terrain generation finished (Minecraft-style multi-stage generation)
    mutable bool m_isEmpty;                 ///< PERFORMANCE: Cached isEmpty state (avoids 32K block scans), updated on setBlock()
    mutable bool m_isEmptyValid;            ///< True if m_isEmpty cache is valid

    // ========== Heightmap (PERFORMANCE: Fast sky light calculation) ==========
    std::array<int16_t, WIDTH * DEPTH> m_heightMap; ///< Highest solid block Y per XZ column (2 KB, 32x32 grid)

    // ========== Interpolated Lighting (Smooth Time-Based Transitions) ==========
    // MEMORY OPTIMIZATION (2025-11-25): Lazy-allocated to save 256KB per cached chunk
    // Only allocated when chunk is actively rendering, deallocated when unloaded
    struct InterpolatedLight {
        float skyLight;    // Current interpolated sky light (0.0-15.0)
        float blockLight;  // Current interpolated block light (0.0-15.0)
        InterpolatedLight() : skyLight(0.0f), blockLight(0.0f) {}
    };
    using InterpolatedLightArray = std::array<InterpolatedLight, WIDTH * HEIGHT * DEPTH>;
    std::unique_ptr<InterpolatedLightArray> m_interpolatedLightData; ///< Lazy-allocated smooth lighting (256 KB when allocated)

    /**
     * @brief Ensures interpolated lighting is allocated (lazy allocation)
     * Called automatically by getInterpolatedXXX and initializeInterpolatedLighting
     */
    void ensureInterpolatedLightingAllocated();

    // ========== Mesh Data (Compressed Vertices for 6x memory savings) ==========
    std::vector<CompressedVertex> m_vertices;         ///< CPU-side vertex data (opaque) - 8 bytes/vertex
    std::vector<uint32_t> m_indices;                  ///< CPU-side index data (opaque)
    std::vector<CompressedVertex> m_transparentVertices;   ///< CPU-side vertex data (transparent) - 8 bytes/vertex
    std::vector<uint32_t> m_transparentIndices;       ///< CPU-side index data (transparent)

    // ========== Vulkan Buffers (Opaque) ==========
    VkBuffer m_vertexBuffer;                ///< GPU vertex buffer (opaque) [LEGACY - will be replaced by mega-buffer]
    VkDeviceMemory m_vertexBufferMemory;    ///< Vertex buffer memory (opaque) [LEGACY]
    VkBuffer m_indexBuffer;                 ///< GPU index buffer (opaque) [LEGACY]
    VkDeviceMemory m_indexBufferMemory;     ///< Index buffer memory (opaque) [LEGACY]
    uint32_t m_vertexCount;                 ///< Number of vertices (opaque)
    uint32_t m_indexCount;                  ///< Number of indices (opaque)

    // Mega-buffer offsets for indirect drawing (GPU optimization)
    VkDeviceSize m_megaBufferVertexOffset = 0;   ///< Offset in mega vertex buffer
    VkDeviceSize m_megaBufferIndexOffset = 0;    ///< Offset in mega index buffer
    uint32_t m_megaBufferBaseVertex = 0;         ///< Base vertex for indexed drawing

    // ========== Vulkan Buffers (Transparent) ==========
    VkBuffer m_transparentVertexBuffer;           ///< GPU vertex buffer (transparent) [LEGACY]
    VkDeviceMemory m_transparentVertexBufferMemory; ///< Vertex buffer memory (transparent) [LEGACY]
    VkBuffer m_transparentIndexBuffer;            ///< GPU index buffer (transparent) [LEGACY]
    VkDeviceMemory m_transparentIndexBufferMemory; ///< Index buffer memory (transparent) [LEGACY]
    uint32_t m_transparentVertexCount;            ///< Number of vertices (transparent)
    uint32_t m_transparentIndexCount;             ///< Number of indices (transparent)

    // Mega-buffer offsets for transparent geometry (indirect drawing)
    VkDeviceSize m_megaBufferTransparentVertexOffset = 0;
    VkDeviceSize m_megaBufferTransparentIndexOffset = 0;
    uint32_t m_megaBufferTransparentBaseVertex = 0;

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

    // ========== RLE Compression Helpers ==========

    /**
     * @brief Compresses block data using Run-Length Encoding
     *
     * RLE is perfect for terrain: layers of stone/dirt/air compress extremely well.
     * Format: [blockID, count, blockID, count, ...]
     * Typical compression: 32KB -> 2-8KB (75-90% reduction!)
     *
     * @param output Vector to write compressed data to
     */
    void compressBlocks(std::vector<uint8_t>& output) const;

    /**
     * @brief Decompresses block data from Run-Length Encoding
     * @param input Compressed data
     * @return True if decompression succeeded, false if data corrupted
     */
    bool decompressBlocks(const std::vector<uint8_t>& input);

    /**
     * @brief Compresses metadata using Run-Length Encoding
     * @param output Vector to write compressed data to
     */
    void compressMetadata(std::vector<uint8_t>& output) const;

    /**
     * @brief Decompresses metadata from Run-Length Encoding
     * @param input Compressed data
     * @return True if decompression succeeded, false if data corrupted
     */
    bool decompressMetadata(const std::vector<uint8_t>& input);

    /**
     * @brief Compresses lighting data using Run-Length Encoding (LIGHTING PERSISTENCE)
     *
     * OPTIMIZATION (2025-11-23): Saves 32 KB lighting to ~1-3 KB compressed.
     * Eliminates 3-5 second lighting recalculation on world load!
     *
     * @param output Vector to write compressed lighting data to
     */
    void compressLighting(std::vector<uint8_t>& output) const;

    /**
     * @brief Decompresses lighting data from Run-Length Encoding
     * @param input Compressed lighting data
     * @return True if decompression succeeded, false if data corrupted
     */
    bool decompressLighting(const std::vector<uint8_t>& input);
};