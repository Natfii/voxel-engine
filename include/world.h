/**
 * @file world.h
 * @brief Voxel world management with chunk-based terrain generation and rendering
 *
 * Enhanced API documentation by Claude (Anthropic AI Assistant)
 */

#pragma once
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <cstdint>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include "chunk.h"
#include "water_simulation.h"
#include "particle_system.h"
#include "biome_map.h"
#include "mesh_buffer_pool.h"

// Forward declarations
class VulkanRenderer;
class BiomeMap;

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
     * @param seed Random seed for world generation (default: 12345)
     */
    World(int width, int height, int depth, int seed = 12345);

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
    void renderWorld(VkCommandBuffer commandBuffer, const glm::vec3& cameraPos, const glm::mat4& viewProj, float renderDistance = 50.0f, class VulkanRenderer* renderer = nullptr);

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
     * @brief Gets the mesh buffer pool for optimized mesh generation
     *
     * The buffer pool reuses vector memory across mesh regenerations,
     * reducing allocation overhead by 40-60%.
     *
     * @return Reference to the mesh buffer pool
     */
    MeshBufferPool& getMeshBufferPool() { return m_meshBufferPool; }

    /**
     * @brief Adds an externally-generated chunk to the world
     *
     * Used by WorldStreaming to integrate asynchronously-generated chunks.
     * Performs duplicate checking and thread-safe insertion.
     *
     * @param chunk Chunk to add (ownership transferred)
     * @return True if chunk was added, false if duplicate/out of bounds
     */
    bool addStreamedChunk(std::unique_ptr<Chunk> chunk);

    /**
     * @brief Removes a chunk from the world
     *
     * Used by WorldStreaming to unload distant chunks.
     * Destroys Vulkan buffers and removes from chunk map.
     *
     * @param chunkX Chunk X coordinate
     * @param chunkY Chunk Y coordinate
     * @param chunkZ Chunk Z coordinate
     * @param renderer Vulkan renderer for buffer cleanup
     * @return True if chunk was removed, false if not found
     */
    bool removeChunk(int chunkX, int chunkY, int chunkZ, VulkanRenderer* renderer);

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
     * By default, regenerates the chunk mesh immediately. For batch operations
     * (like tree placement), pass regenerateMesh=false and manually regenerate
     * meshes after all blocks are placed to avoid performance issues.
     *
     * @param worldX World X coordinate
     * @param worldY World Y coordinate
     * @param worldZ World Z coordinate
     * @param blockID Block ID to set (0 = air/remove block)
     * @param regenerateMesh If true, regenerates mesh immediately (default: true)
     */
    void setBlockAt(float worldX, float worldY, float worldZ, int blockID, bool regenerateMesh = true);

    /**
     * @brief Gets the block metadata at the specified world position
     *
     * @param worldX World X coordinate
     * @param worldY World Y coordinate
     * @param worldZ World Z coordinate
     * @return Metadata value (0-255), or 0 if out of bounds
     */
    uint8_t getBlockMetadataAt(float worldX, float worldY, float worldZ);

    /**
     * @brief Sets the block metadata at the specified world position
     *
     * @param worldX World X coordinate
     * @param worldY World Y coordinate
     * @param worldZ World Z coordinate
     * @param metadata Metadata value to set (0-255)
     */
    void setBlockMetadataAt(float worldX, float worldY, float worldZ, uint8_t metadata);

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

    // ========== Water Simulation ==========

    /**
     * @brief Updates water simulation and particles
     *
     * Should be called every frame to update water flow and particle effects.
     *
     * @param deltaTime Time elapsed since last frame
     * @param renderer Vulkan renderer for buffer recreation
     */
    void updateWaterSimulation(float deltaTime, VulkanRenderer* renderer);

    /**
     * @brief Gets the water simulation system
     * @return Pointer to water simulation
     */
    WaterSimulation* getWaterSimulation() { return m_waterSimulation.get(); }

    /**
     * @brief Gets the particle system
     * @return Pointer to particle system
     */
    ParticleSystem* getParticleSystem() { return m_particleSystem.get(); }

    /**
     * @brief Gets the biome map
     * @return Pointer to biome map
     */
    BiomeMap* getBiomeMap() { return m_biomeMap.get(); }

    /**
     * @brief Runs decoration pass (trees, grass, flowers, structures)
     * Should be called after generateWorld() but before createBuffers()
     */
    void decorateWorld();

    // ========== World Persistence ==========

    /**
     * @brief Saves all chunks to disk
     *
     * Creates world directory structure and saves:
     * - world.meta: World metadata (seed, dimensions)
     * - chunks/*.dat: Binary chunk files
     *
     * @param worldPath Path to world directory (e.g., "worlds/my_world")
     * @return True if save succeeded, false on error
     */
    bool saveWorld(const std::string& worldPath) const;

    /**
     * @brief Loads all chunks from disk
     *
     * Loads world metadata and all chunk files. Skips chunks that don't exist
     * (will be generated instead). Caller must call generateMesh() and
     * createBuffers() after loading.
     *
     * @param worldPath Path to world directory
     * @return True if load succeeded, false if world doesn't exist
     */
    bool loadWorld(const std::string& worldPath);

    /**
     * @brief Gets the world name (extracted from last path component)
     * @return World name string
     */
    std::string getWorldName() const;

    /**
     * @brief Gets the world seed
     * @return World seed value
     */
    int getSeed() const { return m_seed; }

private:
    /**
     * @brief Internal chunk lookup without locking (caller must hold lock)
     *
     * Used internally by functions that already hold m_chunkMapMutex.
     * UNSAFE: Caller must ensure proper locking!
     *
     * @param chunkX Chunk X coordinate
     * @param chunkY Chunk Y coordinate
     * @param chunkZ Chunk Z coordinate
     * @return Pointer to chunk, or nullptr if not found
     */
    Chunk* getChunkAtUnsafe(int chunkX, int chunkY, int chunkZ);

    /**
     * @brief Internal chunk lookup by world position without locking
     * @param worldX World X coordinate
     * @param worldY World Y coordinate
     * @param worldZ World Z coordinate
     * @return Pointer to chunk, or nullptr if not found
     */
    Chunk* getChunkAtWorldPosUnsafe(float worldX, float worldY, float worldZ);

    /**
     * @brief Internal block getter without locking (caller must hold lock)
     * @param worldX World X coordinate
     * @param worldY World Y coordinate
     * @param worldZ World Z coordinate
     * @return Block ID, or 0 if out of bounds
     */
    int getBlockAtUnsafe(float worldX, float worldY, float worldZ);

    /**
     * @brief Internal block setter without locking (caller must hold lock)
     * @param worldX World X coordinate
     * @param worldY World Y coordinate
     * @param worldZ World Z coordinate
     * @param blockID Block ID to set
     */
    void setBlockAtUnsafe(float worldX, float worldY, float worldZ, int blockID);

    /**
     * @brief Internal metadata getter without locking (caller must hold lock)
     * @param worldX World X coordinate
     * @param worldY World Y coordinate
     * @param worldZ World Z coordinate
     * @return Metadata value
     */
    uint8_t getBlockMetadataAtUnsafe(float worldX, float worldY, float worldZ);

    /**
     * @brief Internal metadata setter without locking (caller must hold lock)
     * @param worldX World X coordinate
     * @param worldY World Y coordinate
     * @param worldZ World Z coordinate
     * @param metadata Metadata value to set
     */
    void setBlockMetadataAtUnsafe(float worldX, float worldY, float worldZ, uint8_t metadata);

    int m_width, m_height, m_depth;      ///< World dimensions in chunks
    int m_seed;                          ///< World generation seed
    std::string m_worldName;             ///< World name (extracted from save path)
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>> m_chunkMap;  ///< Fast O(1) chunk lookup by coordinates
    std::vector<Chunk*> m_chunks;  ///< All chunks for iteration (does not own memory)

    // THREAD SAFETY: Protects m_chunkMap access for future chunk streaming
    // Use std::shared_lock for readers (many simultaneous), std::unique_lock for writers (exclusive)
    mutable std::shared_mutex m_chunkMapMutex;

    // WEEK 1 OPTIMIZATION: Mesh buffer pooling (40-60% faster mesh generation)
    // Reuses vector memory across chunk mesh regenerations to avoid allocations
    MeshBufferPool m_meshBufferPool;

    // Water simulation and particles
    std::unique_ptr<WaterSimulation> m_waterSimulation;  ///< Water flow simulation
    std::unique_ptr<ParticleSystem> m_particleSystem;    ///< Particle effects for splashes

    // Biome and generation systems
    std::unique_ptr<BiomeMap> m_biomeMap;  ///< Biome map for world generation
    std::unique_ptr<class TreeGenerator> m_treeGenerator;  ///< Procedural tree generation
};
