/**
 * @file world.h
 * @brief Voxel world management with chunk-based terrain generation and rendering
 *
 * Enhanced API documentation by Claude (Anthropic AI Assistant)
 */

#pragma once
#include <vector>
#include <memory>
#include <unordered_map>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include "chunk.h"

// Forward declaration
class VulkanRenderer;

/**
 * @brief Chunk coordinate key for spatial hash map
 *
 * Used as key in unordered_map for O(1) chunk lookup instead of O(n) linear search.
 */
struct ChunkCoord {
    int x, y, z;

    bool operator==(const ChunkCoord& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

/**
 * @brief Hash function for ChunkCoord to enable use in unordered_map
 */
namespace std {
    template<>
    struct hash<ChunkCoord> {
        size_t operator()(const ChunkCoord& coord) const {
            // Cantor pairing function variation for 3D coordinates
            // Mix coordinates to distribute hash values evenly
            size_t h1 = hash<int>()(coord.x);
            size_t h2 = hash<int>()(coord.y);
            size_t h3 = hash<int>()(coord.z);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };
}

/**
 * @brief Manages the voxel world including chunk generation, rendering, and block operations
 *
 * The World class is the main container for all terrain data in the voxel engine. It handles:
 * - Chunk creation and management in a 3D grid layout centered at the origin
 * - Parallel world generation using FastNoiseLite for procedural terrain
 * - Optimized rendering with frustum culling and distance-based LOD
 * - Block modification with automatic mesh regeneration for affected chunks
 *
 * World coordinates:
 * - Blocks are 0.5 world units in size
 * - World is centered at origin (0, 0, 0)
 * - Each chunk contains 32x32x32 blocks = 16x16x16 world units
 *
 * Performance features:
 * - Multi-threaded chunk generation
 * - Two-stage culling: distance-based + frustum culling
 * - Empty chunk skipping (chunks with no visible geometry)
 *
 * @note The world does not support dynamic chunk loading/unloading - all chunks
 *       are generated at initialization time.
 */
class World {
public:
    /**
     * @brief Constructs a world with the specified dimensions in chunks
     *
     * Creates a grid of chunks centered at the origin. For example, a world with
     * width=12 creates chunks from X=-6 to X=5 (centered around 0).
     *
     * @param width Number of chunks along the X axis
     * @param height Number of chunks along the Y axis (vertical)
     * @param depth Number of chunks along the Z axis
     */
    World(int width, int height, int depth);

    /**
     * @brief Destroys the world and cleans up all chunks
     *
     * @note Vulkan buffers must be cleaned up separately via cleanup() before destruction
     */
    ~World();

    /**
     * @brief Generates terrain for all chunks using FastNoiseLite
     *
     * Uses multi-threaded generation for improved performance. The number of threads
     * is automatically determined based on hardware concurrency.
     *
     * @note Must be called before createBuffers()
     */
    void generateWorld();

    /**
     * @brief Creates Vulkan buffers for all non-empty chunks
     *
     * Skips chunks with no vertices to save GPU memory. Must be called after
     * generateWorld() and before rendering.
     *
     * @param renderer Vulkan renderer for buffer creation
     */
    void createBuffers(VulkanRenderer* renderer);

    /**
     * @brief Destroys all Vulkan buffers for cleanup
     *
     * Must be called before the renderer is destroyed to prevent memory leaks.
     *
     * @param renderer Vulkan renderer that created the buffers
     */
    void cleanup(VulkanRenderer* renderer);

    /**
     * @brief Renders visible chunks with frustum and distance culling
     *
     * Implements two-stage culling:
     * 1. Distance culling: Eliminates chunks beyond render distance
     * 2. Frustum culling: Eliminates chunks outside the camera view
     *
     * Culling includes margins to prevent chunk popping at edges.
     *
     * @param commandBuffer Vulkan command buffer for draw calls
     * @param cameraPos Current camera position in world space
     * @param viewProj Combined view-projection matrix for frustum extraction
     * @param renderDistance Maximum render distance in world units (default: 50.0)
     */
    void renderWorld(VkCommandBuffer commandBuffer, const glm::vec3& cameraPos, const glm::mat4& viewProj, float renderDistance = 50.0f);

    // ========== Block Querying and Modification ==========

    /**
     * @brief Gets the chunk at the specified chunk coordinates
     *
     * @param chunkX Chunk X coordinate
     * @param chunkY Chunk Y coordinate
     * @param chunkZ Chunk Z coordinate
     * @return Pointer to the chunk, or nullptr if out of bounds
     */
    Chunk* getChunkAt(int chunkX, int chunkY, int chunkZ);

    /**
     * @brief Gets the chunk containing the specified world position
     *
     * Converts world coordinates to chunk coordinates and returns the containing chunk.
     *
     * @param worldX World X coordinate
     * @param worldY World Y coordinate
     * @param worldZ World Z coordinate
     * @return Pointer to the chunk, or nullptr if out of bounds
     */
    Chunk* getChunkAtWorldPos(float worldX, float worldY, float worldZ);

    /**
     * @brief Gets the block ID at the specified world position
     *
     * @param worldX World X coordinate
     * @param worldY World Y coordinate
     * @param worldZ World Z coordinate
     * @return Block ID (0 = air, >0 = solid block), or 0 if out of bounds
     */
    int getBlockAt(float worldX, float worldY, float worldZ);

    /**
     * @brief Sets the block ID at the specified world position
     *
     * Does not update meshes or buffers. Use breakBlock() for automatic mesh updates.
     *
     * @param worldX World X coordinate
     * @param worldY World Y coordinate
     * @param worldZ World Z coordinate
     * @param blockID Block ID to set (0 = air/remove block)
     */
    void setBlockAt(float worldX, float worldY, float worldZ, int blockID);

    // ========== Higher-Level Block Operations ==========

    /**
     * @brief Breaks a block and regenerates meshes for affected chunks
     *
     * Removes the block at the specified position and automatically:
     * - Regenerates the mesh for the affected chunk
     * - Updates all 6 adjacent chunks (for proper face culling)
     * - Recreates Vulkan buffers for modified chunks
     *
     * This ensures no visual artifacts when blocks are removed.
     *
     * @param worldX World X coordinate
     * @param worldY World Y coordinate
     * @param worldZ World Z coordinate
     * @param renderer Vulkan renderer for buffer recreation
     */
    void breakBlock(float worldX, float worldY, float worldZ, VulkanRenderer* renderer);

    /**
     * @brief Breaks a block at the specified position (vec3 overload)
     *
     * @param position World position as glm::vec3
     * @param renderer Vulkan renderer for buffer recreation
     */
    void breakBlock(const glm::vec3& position, VulkanRenderer* renderer);

    /**
     * @brief Breaks a block at the specified block coordinates (ivec3 overload)
     *
     * Converts block coordinates to world coordinates before breaking.
     * Block coordinates are in blocks (not world units).
     *
     * @param coords Block coordinates as glm::ivec3
     * @param renderer Vulkan renderer for buffer recreation
     */
    void breakBlock(const glm::ivec3& coords, VulkanRenderer* renderer);

    /**
     * @brief Places a block and regenerates meshes for affected chunks
     *
     * Places the specified block type at the given position and automatically:
     * - Regenerates the mesh for the affected chunk
     * - Updates all 6 adjacent chunks (for proper face culling)
     * - Recreates Vulkan buffers for modified chunks
     *
     * @param worldX World X coordinate
     * @param worldY World Y coordinate
     * @param worldZ World Z coordinate
     * @param blockID Block ID to place (>0)
     * @param renderer Vulkan renderer for buffer recreation
     */
    void placeBlock(float worldX, float worldY, float worldZ, int blockID, VulkanRenderer* renderer);

    /**
     * @brief Places a block at the specified position (vec3 overload)
     *
     * @param position World position as glm::vec3
     * @param blockID Block ID to place (>0)
     * @param renderer Vulkan renderer for buffer recreation
     */
    void placeBlock(const glm::vec3& position, int blockID, VulkanRenderer* renderer);

    // ========== Liquid Physics ==========

    /**
     * @brief Updates liquid blocks to simulate gravity and flow
     *
     * Checks all liquid blocks and makes them fall if there's air below.
     * This should be called periodically from the main game loop.
     *
     * @param renderer Vulkan renderer for buffer recreation
     */
    void updateLiquids(VulkanRenderer* renderer);

private:
    int m_width, m_height, m_depth;      ///< World dimensions in chunks
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>> m_chunkMap;  ///< Fast O(1) chunk lookup by coordinates
    std::vector<Chunk*> m_chunks;  ///< All chunks for iteration (does not own memory)
};
