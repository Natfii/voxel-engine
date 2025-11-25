/**
 * @file world_streaming.h
 * @brief Asynchronous chunk loading/unloading system for infinite worlds
 *
 * ARCHITECTURE:
 * - Priority queue orders chunks by distance from player
 * - Worker threads generate/load chunks in background
 * - Main thread handles mesh creation and buffer upload (Vulkan not thread-safe)
 * - Double-buffering pattern: generation happens async, mesh upload on main thread
 *
 * THREAD SAFETY:
 * - Chunk map protected by shared_mutex (readers: many, writers: exclusive)
 * - Load queue protected by mutex + condition variable
 * - Atomic flags for shutdown signaling
 *
 * PERFORMANCE:
 * - Configurable worker thread count (default: hardware_concurrency - 1)
 * - Chunk pooling to reuse memory (40-60% speedup)
 * - Priority-based loading prevents frame stutter
 *
 * Created: 2025-11-14
 * Part of Week 1 threading and streaming optimizations
 */

#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <functional>
#include <unordered_set>
#include <chrono>
#include <glm/glm.hpp>

// Need full ChunkCoord definition for hash function in unordered_set
#include "world.h"

// Forward declarations
class Chunk;
class VulkanRenderer;
class BiomeMap;

/**
 * @brief LOD tier for chunk loading (2025-11-25)
 *
 * Tiered loading reduces work for distant chunks:
 * - FULL: Close chunks get decoration + full mesh (visible)
 * - MESH_ONLY: Medium chunks skip decoration, get mesh (fog hides detail)
 * - TERRAIN_ONLY: Far chunks skip decoration AND mesh (beyond render distance)
 */
enum class ChunkLOD : uint8_t {
    FULL = 0,           ///< Full detail: decoration + mesh (within ~48 blocks)
    MESH_ONLY = 1,      ///< Medium detail: mesh only, no decoration (48-80 blocks, fog hides trees)
    TERRAIN_ONLY = 2    ///< Terrain only: no decoration, no mesh (>80 blocks, not rendered)
};

/**
 * @brief Chunk loading request with priority and LOD tier
 *
 * Chunks closer to the player have higher priority (lower distance).
 * The priority queue orders by distance (smaller = higher priority).
 */
struct ChunkLoadRequest {
    int chunkX, chunkY, chunkZ;      ///< Chunk coordinates to load
    float priority;                   ///< Priority (distance from player, lower = higher priority)
    ChunkLOD lod = ChunkLOD::FULL;   ///< LOD tier based on distance

    /**
     * @brief Comparison operator for priority queue
     *
     * Note: We use > because priority_queue is a max-heap by default,
     * but we want min-heap behavior (smallest distance first)
     */
    bool operator<(const ChunkLoadRequest& other) const {
        return priority > other.priority;  // Inverted: smaller distance = higher priority
    }
};

/**
 * @brief Manages asynchronous chunk streaming for infinite worlds
 *
 * The WorldStreaming class handles:
 * - Background chunk generation on worker threads
 * - Priority-based loading (closest chunks first)
 * - Automatic unloading of distant chunks
 * - Thread-safe coordination with main rendering thread
 *
 * Usage:
 * @code
 *   WorldStreaming streaming(world, biomeMap, renderer);
 *   streaming.start(4);  // Start with 4 worker threads
 *
 *   // Each frame:
 *   streaming.updatePlayerPosition(playerPos);
 *   streaming.processCompletedChunks();  // Upload ready chunks on main thread
 *
 *   // On shutdown:
 *   streaming.stop();
 * @endcode
 *
 * Thread Model:
 * - Worker threads: Generate terrain + mesh (CPU-only operations)
 * - Main thread: Create Vulkan buffers (GPU operations, not thread-safe)
 */
class WorldStreaming {
public:
    /**
     * @brief Constructs a streaming manager for the world
     *
     * @param world World instance to manage
     * @param biomeMap Biome map for terrain generation
     * @param renderer Vulkan renderer for buffer creation
     */
    WorldStreaming(World* world, BiomeMap* biomeMap, VulkanRenderer* renderer);

    /**
     * @brief Destructor - ensures worker threads are stopped
     */
    ~WorldStreaming();

    // Prevent copying
    WorldStreaming(const WorldStreaming&) = delete;
    WorldStreaming& operator=(const WorldStreaming&) = delete;

    /**
     * @brief Starts background worker threads
     *
     * @param numWorkers Number of worker threads (default: hardware_concurrency - 1)
     */
    void start(int numWorkers = 0);

    /**
     * @brief Stops all worker threads and waits for completion
     *
     * Signals workers to exit and joins all threads.
     * Safe to call multiple times.
     */
    void stop();

    /**
     * @brief Updates player position and schedules chunk loading
     *
     * Should be called each frame. Determines which chunks to load/unload
     * based on player position and render distance.
     *
     * @param playerPos Current player position in world space
     * @param loadDistance Maximum distance to load chunks (default: 64.0)
     * @param unloadDistance Distance at which to unload chunks (default: 96.0)
     */
    void updatePlayerPosition(const glm::vec3& playerPos,
                             float loadDistance = 64.0f,
                             float unloadDistance = 96.0f);

    /**
     * @brief Processes chunks that finished generation
     *
     * MUST be called on the main thread (Vulkan operations).
     * Creates Vulkan buffers for chunks that finished generating.
     * Call this once per frame after updatePlayerPosition().
     *
     * @param maxChunksPerFrame Maximum chunks to upload per frame (prevents frame stutter)
     * @param maxMilliseconds Maximum time budget for chunk processing in milliseconds (0 = unlimited)
     */
    void processCompletedChunks(int maxChunksPerFrame = 4, float maxMilliseconds = 8.0f);

    /**
     * @brief Gets the number of chunks in the load queue
     * @return Number of pending chunk load requests
     */
    size_t getPendingLoadCount() const;

    /**
     * @brief Gets the number of chunks waiting for buffer upload
     * @return Number of chunks ready for GPU upload
     */
    size_t getCompletedChunkCount() const;

    /**
     * @brief Gets statistics about the streaming system
     * @return Tuple of (pending loads, completed chunks, active workers)
     */
    std::tuple<size_t, size_t, int> getStats() const;

    /**
     * @brief Checks if streaming is currently active
     * @return True if worker threads are running
     */
    bool isActive() const { return m_running.load(); }

    /**
     * @brief Gets the mesh generation queue size
     * @return Number of chunks waiting for mesh generation
     */
    size_t getMeshQueueSize() const {
        std::lock_guard<std::mutex> lock(m_meshQueueMutex);
        return m_meshWorkQueue.size();
    }

    /**
     * @brief Queue a chunk for async mesh generation (used by decoration system)
     *
     * This allows decorated chunks to use the same async mesh pipeline as streamed chunks.
     * The chunk will be meshed by worker threads and queued for GPU upload.
     *
     * @param chunkX Chunk X coordinate
     * @param chunkY Chunk Y coordinate
     * @param chunkZ Chunk Z coordinate
     */
    void queueChunkForMeshing(int chunkX, int chunkY, int chunkZ);

    /**
     * @brief Sets the spawn anchor point (like Minecraft spawn chunks)
     *
     * Chunks within the anchor radius will NEVER be unloaded, staying permanently
     * in memory for instant access. This mimics Minecraft's "spawn chunks" behavior
     * where chunks around world origin stay loaded for redstone farms, etc.
     *
     * @param chunkX Spawn chunk X coordinate
     * @param chunkY Spawn chunk Y coordinate
     * @param chunkZ Spawn chunk Z coordinate
     * @param radius Radius in chunks to keep loaded (default: 6 = 13×13×13 cube)
     */
    void setSpawnAnchor(int chunkX, int chunkY, int chunkZ, int radius = 6);

    /**
     * @brief Checks if a chunk is within the spawn anchor (never unloaded)
     * @return True if chunk should never be unloaded
     */
    bool isInSpawnAnchor(int chunkX, int chunkY, int chunkZ) const;

    // ============================================================================
    // BACKGROUND PRE-GENERATION (2025-11-25)
    // ============================================================================

    /**
     * @brief Enable predictive pre-generation in player's movement direction
     *
     * When enabled, the system will generate chunks ahead of the player
     * based on movement velocity. This reduces pop-in when moving fast.
     *
     * @param enabled Enable/disable predictive generation
     * @param lookAheadDistance How far ahead to pre-generate (default: 96 blocks)
     */
    void setPredictiveGeneration(bool enabled, float lookAheadDistance = 96.0f);

    /**
     * @brief Queue chunks for background pre-generation
     *
     * Called during idle time to pre-generate chunks beyond normal load distance.
     * Uses a lower priority than player-proximity chunks.
     *
     * @param centerX Center X coordinate (world space)
     * @param centerZ Center Z coordinate (world space)
     * @param radius Radius in chunks to pre-generate
     */
    void queueBackgroundGeneration(int centerX, int centerZ, int radius);

private:
    /**
     * @brief Worker thread main loop
     *
     * Continuously pulls requests from the load queue and generates chunks.
     * Runs until m_running is set to false.
     */
    void workerThreadFunction();

    /**
     * @brief Mesh worker thread main loop (PERFORMANCE FIX 2025-11-24)
     *
     * Continuously pulls chunks from the mesh work queue and generates meshes.
     * Eliminates 600+ thread creations/sec overhead from detached thread approach.
     * Runs until m_meshWorkersRunning is set to false.
     */
    void meshWorkerThreadFunction();

    /**
     * @brief Generates a single chunk (terrain + mesh)
     *
     * Called by worker threads. CPU-only operations (thread-safe).
     *
     * @param chunkX Chunk X coordinate
     * @param chunkY Chunk Y coordinate
     * @param chunkZ Chunk Z coordinate
     * @return Generated chunk (ownership transferred to caller)
     */
    std::unique_ptr<Chunk> generateChunk(int chunkX, int chunkY, int chunkZ);

    /**
     * @brief Checks if a chunk should be loaded based on player position
     *
     * @param chunkX Chunk X coordinate
     * @param chunkY Chunk Y coordinate
     * @param chunkZ Chunk Z coordinate
     * @param playerPos Player position
     * @param loadDistance Maximum load distance
     * @return True if chunk is within load distance
     */
    bool shouldLoadChunk(int chunkX, int chunkY, int chunkZ,
                        const glm::vec3& playerPos, float loadDistance) const;

    /**
     * @brief Calculates chunk priority (distance from player)
     *
     * @param chunkX Chunk X coordinate
     * @param chunkY Chunk Y coordinate
     * @param chunkZ Chunk Z coordinate
     * @param playerPos Player position
     * @return Distance from player to chunk center (lower = higher priority)
     */
    float calculateChunkPriority(int chunkX, int chunkY, int chunkZ,
                                 const glm::vec3& playerPos) const;

    /**
     * @brief Converts chunk coordinates to world position (center of chunk)
     *
     * @param chunkX Chunk X coordinate
     * @param chunkY Chunk Y coordinate
     * @param chunkZ Chunk Z coordinate
     * @return World position of chunk center
     */
    glm::vec3 chunkToWorldPos(int chunkX, int chunkY, int chunkZ) const;


    /**
     * @brief Tracks a failed chunk generation attempt
     *
     * @param chunkX Chunk X coordinate
     * @param chunkY Chunk Y coordinate
     * @param chunkZ Chunk Z coordinate
     * @param errorMsg Error message
     */
    void trackFailedChunk(int chunkX, int chunkY, int chunkZ, const std::string& errorMsg);

    /**
     * @brief Retries failed chunks with exponential backoff
     *
     * Checks failed chunks and retries those that have waited long enough.
     * Uses exponential backoff: 1s, 2s, 4s, 8s, etc.
     * Gives up after 5 attempts.
     */
    void retryFailedChunks();

    // === Core References ===
    World* m_world;                       ///< World instance being managed
    BiomeMap* m_biomeMap;                 ///< Biome map for generation
    VulkanRenderer* m_renderer;           ///< Renderer for buffer creation

    // === Threading ===
    std::vector<std::thread> m_workers;   ///< Background worker threads
    std::atomic<bool> m_running;          ///< Worker thread running flag
    std::atomic<int> m_activeWorkers;     ///< Number of active workers

    // === Load Queue (accessed by main thread + workers) ===
    std::priority_queue<ChunkLoadRequest> m_loadQueue;  ///< Priority queue of chunks to load
    mutable std::mutex m_loadQueueMutex;                ///< Protects m_loadQueue
    std::condition_variable m_loadQueueCV;              ///< Signals workers when work available

    // === Deduplication Tracking ===
    std::unordered_set<ChunkCoord> m_chunksInFlight;   ///< Tracks chunks being generated (prevents duplicates)

    // === Error Tracking and Retry ===
    struct FailedChunk {
        ChunkCoord coord;                ///< Chunk coordinates
        int failureCount;                ///< Number of failed attempts
        std::chrono::steady_clock::time_point lastAttempt;  ///< Time of last attempt
        std::string errorMessage;        ///< Last error message
    };
    std::vector<FailedChunk> m_failedChunks;       ///< Chunks that failed to generate
    mutable std::mutex m_failedChunksMutex;        ///< Protects m_failedChunks

    // === Completed Chunks (accessed by workers + main thread) ===
    struct CompletedChunk {
        std::unique_ptr<Chunk> chunk;
        ChunkLOD lod;
    };
    std::vector<CompletedChunk> m_completedChunks;  ///< Chunks ready for processing (with LOD)
    mutable std::mutex m_completedMutex;            ///< Protects m_completedChunks

    // === Async Mesh Generation (PERFORMANCE FIX 2025-11-23) ===
    // Chunks that have finished mesh generation and are ready for GPU upload
    // Background threads push here after meshing, main thread pops for upload
    std::queue<std::tuple<int, int, int>> m_chunksReadyForUpload;  ///< Chunks ready for GPU upload
    mutable std::mutex m_readyForUploadMutex;                      ///< Protects m_chunksReadyForUpload

    // CRITICAL BUG FIX: Prevent chunk deletion during async mesh generation
    // Tracks chunks currently being meshed by detached threads
    // removeChunk() checks this set and defers deletion until meshing completes
    std::unordered_set<ChunkCoord> m_chunksBeingMeshed;  ///< Chunks with active mesh generation threads
    mutable std::mutex m_chunksMeshingMutex;             ///< Protects m_chunksBeingMeshed

    // === Mesh Thread Pool (PERFORMANCE FIX 2025-11-24) ===
    // Thread pool for mesh generation - eliminates 600+ thread creations/sec overhead
    // OLD: Spawn detached thread per chunk (600/sec thread spawning storm)
    // NEW: 4-8 persistent workers pull from queue (zero thread creation overhead)
    std::queue<std::tuple<int, int, int>> m_meshWorkQueue;  ///< Chunks waiting for mesh generation
    mutable std::mutex m_meshQueueMutex;                    ///< Protects m_meshWorkQueue
    std::condition_variable m_meshQueueCV;                  ///< Wake mesh workers when work available
    std::vector<std::thread> m_meshWorkers;                 ///< Mesh worker thread pool
    std::atomic<bool> m_meshWorkersRunning;                 ///< Flag to shutdown mesh workers

    // === Player Position ===
    glm::vec3 m_lastPlayerPos;              ///< Last known player position
    glm::vec3 m_previousPlayerPos;          ///< Previous player position for velocity calculation
    std::chrono::high_resolution_clock::time_point m_lastVelocityUpdate;  ///< Last time velocity was calculated
    float m_playerVelocity;                 ///< Current player velocity (blocks/sec)
    mutable std::mutex m_playerPosMutex;    ///< Protects m_lastPlayerPos and velocity data

    // PERFORMANCE FIX (2025-11-24): Track last chunk to avoid 13,500 iterations/sec
    // Only run expensive cube iteration (15×15×15 = 3,375 checks) when player crosses chunk boundary
    std::tuple<int, int, int> m_lastPlayerChunk;  ///< Last chunk coordinates (x, y, z)
    mutable std::mutex m_playerChunkMutex;        ///< Protects m_lastPlayerChunk

    // === Statistics ===
    std::atomic<size_t> m_totalChunksLoaded;    ///< Total chunks loaded since start
    std::atomic<size_t> m_totalChunksUnloaded;  ///< Total chunks unloaded since start

    // === Spawn Anchor (Minecraft-style spawn chunks) ===
    // Chunks within anchor radius NEVER unload - stay in memory permanently
    int m_spawnAnchorX = 0;      ///< Spawn anchor chunk X
    int m_spawnAnchorY = 0;      ///< Spawn anchor chunk Y
    int m_spawnAnchorZ = 0;      ///< Spawn anchor chunk Z
    int m_spawnAnchorRadius = 0; ///< Radius of spawn chunks (0 = disabled)
    bool m_spawnAnchorEnabled = false;  ///< True if spawn anchor is set

    // === Predictive Pre-Generation (2025-11-25) ===
    // Generates chunks ahead in player's movement direction
    bool m_predictiveEnabled = true;    ///< Enable/disable predictive generation
    float m_lookAheadDistance = 96.0f;  ///< How far ahead to pre-generate (blocks)
    glm::vec3 m_playerMovementDir{0.0f}; ///< Normalized movement direction
};
