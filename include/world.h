/**
 * @file world.h
 * @brief Voxel world management with chunk-based terrain generation and rendering
 *
 */

#pragma once
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <functional>
#include <cstdint>
#include <future>
#include <chrono>
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
class LightingSystem;

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
    // Note: hash<glm::ivec3> is already defined in water_simulation.h
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
 * - Blocks are 1.0 world units in size
 * - World is centered at origin (0, 0, 0)
 * - Each chunk contains 32x32x32 blocks = 32x32x32 world units
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
    friend class Chunk;  // Allow Chunk to access unsafe methods when holding lock
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
     * @param tempBias Temperature bias (-1.0 to +1.0)
     * @param moistBias Moisture bias (-1.0 to +1.0)
     * @param ageBias Age/roughness bias (-1.0 to +1.0)
     */
    World(int width, int height, int depth, int seed = 12345, float tempBias = 0.0f, float moistBias = 0.0f, float ageBias = 0.0f);

    /**
     * @brief Destroys the world and cleans up all chunks
     *
     * @note Vulkan buffers must be cleaned up separately via cleanup() before destruction
     */
    ~World();

    /**
     * @brief Generates only chunks in a radius around a spawn point
     *
     * Optimized for fast startup - generates only the chunks needed around spawn.
     * The WorldStreaming system handles the rest dynamically as the player moves.
     *
     * @param centerChunkX Center chunk X coordinate
     * @param centerChunkY Center chunk Y coordinate
     * @param centerChunkZ Center chunk Z coordinate
     * @param radius Radius in chunks (e.g., 3 = 7x7x7 chunk cube = 343 chunks)
     */
    void generateSpawnChunks(int centerChunkX, int centerChunkY, int centerChunkZ, int radius = 3);

    /**
     * @brief Generates terrain for all chunks using FastNoiseLite
     *
     * DEPRECATED: Too slow for large worlds (e.g., 320 height = 46,080 chunks)
     * Use generateSpawnChunks() + WorldStreaming instead for better performance.
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
     * @brief Gets coordinates of all currently loaded chunks
     *
     * Used for efficient chunk unloading - iterates only loaded chunks
     * instead of a large volume. Thread-safe with shared_lock.
     *
     * @return Vector of chunk coordinates for all loaded chunks
     */
    std::vector<ChunkCoord> getAllChunkCoords() const;

    /**
     * @brief Iterates chunk coordinates with a callback (zero-copy)
     *
     * More efficient than getAllChunkCoords() - avoids copying 432 coords.
     * Holds shared_lock during iteration, so callback must be fast.
     *
     * @param callback Function called for each chunk coordinate
     */
    void forEachChunkCoord(const std::function<void(const ChunkCoord&)>& callback) const;

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
     * After adding, decorates, lights, regenerates mesh, and uploads to GPU.
     *
     * @param chunk Chunk to add (ownership transferred)
     * @param renderer Vulkan renderer for buffer creation (after decoration/lighting)
     * @param deferGPUUpload If true, mesh generation happens but GPU upload is deferred
     * @param deferMeshGeneration If true, even mesh generation is deferred
     * @return True if chunk was added, false if duplicate/out of bounds
     */
    bool addStreamedChunk(std::unique_ptr<Chunk> chunk, VulkanRenderer* renderer, bool deferGPUUpload = false, bool deferMeshGeneration = false);

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
     * @param skipWaterCleanup Skip water cleanup (used when batch cleanup already done)
     * @return True if chunk was removed, false if not found
     */
    bool removeChunk(int chunkX, int chunkY, int chunkZ, VulkanRenderer* renderer, bool skipWaterCleanup = false);

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
     * Only simulates water within render distance (chunk freezing optimization).
     *
     * @param deltaTime Time elapsed since last frame
     * @param renderer Vulkan renderer for buffer recreation
     * @param playerPos Player's position in world coordinates
     * @param renderDistance Maximum distance from player to simulate water
     */
    void updateWaterSimulation(float deltaTime, VulkanRenderer* renderer, const glm::vec3& playerPos, float renderDistance);

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
     * @brief Gets the lighting system
     * @return Pointer to lighting system
     */
    LightingSystem* getLightingSystem() { return m_lightingSystem.get(); }

    /**
     * @brief Initializes basic sunlight for a newly generated chunk
     * @param chunk Chunk to initialize lighting for
     */
    void initializeChunkLighting(Chunk* chunk);

    /**
     * @brief Checks if chunk coordinates are within world bounds
     *
     * @param chunkX Chunk X coordinate
     * @param chunkY Chunk Y coordinate
     * @param chunkZ Chunk Z coordinate
     * @return True if chunk is within bounds, false otherwise
     */
    bool isChunkInBounds(int chunkX, int chunkY, int chunkZ) const;

    /**
     * @brief Runs decoration pass (trees, grass, flowers, structures)
     * Should be called after generateWorld() but before createBuffers()
     */
    void decorateWorld();

    /**
     * @brief Decorates a single chunk (for streaming chunks)
     *
     * Places trees and features in a freshly generated chunk.
     * Uses deterministic seeding based on chunk coordinates.
     *
     * @param chunk The chunk to decorate
     */
    void decorateChunk(Chunk* chunk);

    /**
     * @brief Checks if a chunk has all horizontal neighbors loaded (needed for decoration)
     *
     * Decorations like trees can extend into neighboring chunks, so we need to ensure
     * all 4 horizontal neighbors (at the same Y level) exist before decorating.
     *
     * @param chunk Chunk to check
     * @return True if all 4 horizontal neighbors exist
     */
    bool hasHorizontalNeighbors(Chunk* chunk);

    /**
     * @brief Attempts to decorate pending chunks that now have neighbors
     *
     * Called periodically to retry decoration on chunks that were skipped
     * because their neighbors weren't loaded yet.
     *
     * @param renderer Vulkan renderer for mesh/buffer updates
     * @param maxChunks Maximum chunks to process this call (default: 5)
     */
    void processPendingDecorations(VulkanRenderer* renderer, int maxChunks = 5);

    /**
     * @brief Updates interpolated lighting for all loaded chunks
     *
     * Creates smooth, natural lighting transitions over time.
     *
     * @param deltaTime Time since last frame in seconds
     */
    void updateInterpolatedLighting(float deltaTime);

    /**
     * @brief Register water blocks in a single chunk with the simulation system
     * Called for both initial world generation and dynamically loaded chunks
     */
    void registerWaterInChunk(Chunk* chunk);

    /**
     * @brief Scans all generated chunks and registers water blocks with simulation
     * Should be called after chunk generation to initialize water flow physics
     */
    void registerWaterBlocks();

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
     * @brief Saves only modified chunks to disk (autosave)
     *
     * Batch-writes all dirty chunks from the cache. Called by autosave timer
     * and on game exit. Much more efficient than saving all chunks.
     *
     * @return Number of chunks saved
     */
    int saveModifiedChunks();

    /**
     * @brief Marks a chunk as modified (needs saving)
     *
     * Called when blocks are placed/broken. Ensures chunk gets saved
     * during next autosave.
     *
     * @param chunkX Chunk X coordinate
     * @param chunkY Chunk Y coordinate
     * @param chunkZ Chunk Z coordinate
     */
    void markChunkDirty(int chunkX, int chunkY, int chunkZ);

    /**
     * @brief Marks a chunk as dirty (UNSAFE - caller must hold m_chunkMapMutex)
     * @param chunkX Chunk X coordinate
     * @param chunkY Chunk Y coordinate
     * @param chunkZ Chunk Z coordinate
     */
    void markChunkDirtyUnsafe(int chunkX, int chunkY, int chunkZ);

    /**
     * @brief Retrieves chunk from RAM cache (if present)
     *
     * Checks m_unloadedChunksCache for chunks that were unloaded but kept in RAM.
     * Returns nullptr if not in cache. This is 10,000x faster than disk loading!
     *
     * @param chunkX Chunk X coordinate
     * @param chunkY Chunk Y coordinate
     * @param chunkZ Chunk Z coordinate
     * @return unique_ptr to chunk if in cache, nullptr otherwise
     */
    std::unique_ptr<Chunk> getChunkFromCache(int chunkX, int chunkY, int chunkZ);

    /**
     * @brief Acquires a chunk from the pool (or creates new if pool empty)
     *
     * CHUNK POOLING: Reuses chunks from pool instead of new/delete.
     * 100x faster than allocation for chunk creation!
     *
     * @param chunkX Chunk X coordinate
     * @param chunkY Chunk Y coordinate
     * @param chunkZ Chunk Z coordinate
     * @return unique_ptr to chunk (either from pool or freshly allocated)
     */
    std::unique_ptr<Chunk> acquireChunk(int chunkX, int chunkY, int chunkZ);

    /**
     * @brief Returns a chunk to the pool for reuse
     *
     * If pool is not full, adds chunk to pool. Otherwise destroys it.
     * Caller must ensure Vulkan buffers are destroyed first!
     *
     * @param chunk Chunk to return to pool
     */
    void releaseChunk(std::unique_ptr<Chunk> chunk);

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
     * @brief Gets the world save path
     * @return World path string (e.g., "worlds/world_12345")
     */
    std::string getWorldPath() const { return m_worldPath; }

    /**
     * @brief Gets the world seed
     * @return World seed value
     */
    int getSeed() const { return m_seed; }

    /**
     * @brief Gets all chunks for iteration
     * @return Reference to vector of all chunks
     */
    std::vector<Chunk*>& getChunks() { return m_chunks; }
    const std::vector<Chunk*>& getChunks() const { return m_chunks; }

    /**
     * @brief Internal block getter without locking (caller must hold lock)
     *
     * THREAD SAFETY: This method does NOT acquire any locks. The caller MUST
     * already hold m_chunkMapMutex before calling this method.
     *
     * Use this when you already hold the lock to prevent deadlock.
     * For normal use, call getBlockAt() instead.
     *
     * @param worldX World X coordinate
     * @param worldY World Y coordinate
     * @param worldZ World Z coordinate
     * @return Block ID, or 0 if out of bounds
     */
    int getBlockAtUnsafe(float worldX, float worldY, float worldZ);

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
    std::string m_worldPath;             ///< World save path for chunk streaming persistence
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>> m_chunkMap;  ///< Fast O(1) chunk lookup by coordinates
    std::vector<Chunk*> m_chunks;  ///< All chunks for iteration (does not own memory)

    // CHUNK CACHING: RAM cache for unloaded chunks (prevents disk thrashing)
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>> m_unloadedChunksCache;  ///< Cached unloaded chunks (still in RAM)
    std::unordered_set<ChunkCoord> m_dirtyChunks;  ///< Chunks modified since last save (need disk write)
    mutable std::mutex m_dirtyChunksMutex;  ///< THREAD SAFETY (2025-11-23): Protects m_dirtyChunks for parallel decoration
    size_t m_maxCachedChunks = 5000;  ///< Maximum cached chunks before forced eviction (~490MB at 98KB/chunk)

    // CHUNK POOLING: Reuse chunk objects instead of new/delete (100x faster allocation)
    std::vector<std::unique_ptr<Chunk>> m_chunkPool;  ///< Pool of reusable chunk objects
    size_t m_maxPoolSize = 500;  ///< Maximum pooled chunks (32MB at 64KB/chunk)
    mutable std::mutex m_chunkPoolMutex;  ///< Protects m_chunkPool access from worker threads

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

    // Lighting system
    std::unique_ptr<LightingSystem> m_lightingSystem;  ///< Voxel lighting system

    // DECORATION FIX: Track chunks waiting for neighbors before decoration
    std::unordered_set<Chunk*> m_pendingDecorations;  ///< Chunks waiting for neighbors to be decorated
    mutable std::mutex m_pendingDecorationsMutex;  ///< THREAD SAFETY (2025-11-23): Protects m_pendingDecorations

    // ASYNC DECORATION PIPELINE (2025-11-24): Track decorations in progress (don't block main thread!)
    struct DecorationTask {
        Chunk* chunk;
        std::future<void> future;
        std::chrono::steady_clock::time_point startTime;
    };
    std::vector<DecorationTask> m_decorationsInProgress;  ///< Decorations running in background
    mutable std::mutex m_decorationsInProgressMutex;  ///< Protects m_decorationsInProgress

    // WATER PERFORMANCE FIX: Track water blocks that need flow updates (dirty list)
    std::unordered_set<glm::ivec3> m_dirtyWaterBlocks;  ///< Water blocks that changed and need flow update

    // RENDERING OPTIMIZATION: Cache transparent chunk sort position to avoid re-sorting every frame
    glm::vec3 m_lastSortPosition = glm::vec3(0.0f);  ///< Last camera position used for sorting transparent chunks
};
