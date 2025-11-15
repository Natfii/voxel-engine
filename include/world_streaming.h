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
 * @brief Chunk loading request with priority
 *
 * Chunks closer to the player have higher priority (lower distance).
 * The priority queue orders by distance (smaller = higher priority).
 */
struct ChunkLoadRequest {
    int chunkX, chunkY, chunkZ;      ///< Chunk coordinates to load
    float priority;                   ///< Priority (distance from player, lower = higher priority)

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
     * @param loadDistance Maximum distance to load chunks (default: 128.0)
     * @param unloadDistance Distance at which to unload chunks (default: 160.0)
     * @param cameraForward Camera forward direction (for priority calculation)
     */
    void updatePlayerPosition(const glm::vec3& playerPos,
                             float loadDistance = 128.0f,
                             float unloadDistance = 160.0f,
                             const glm::vec3& cameraForward = glm::vec3(0.0f, 0.0f, -1.0f));

    /**
     * @brief Processes chunks that finished generation
     *
     * MUST be called on the main thread (Vulkan operations).
     * Creates Vulkan buffers for chunks that finished generating.
     * Call this once per frame after updatePlayerPosition().
     *
     * @param maxChunksPerFrame Maximum chunks to upload per frame (prevents frame stutter)
     */
    void processCompletedChunks(int maxChunksPerFrame = 4);

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

private:
    /**
     * @brief Worker thread main loop
     *
     * Continuously pulls requests from the load queue and generates chunks.
     * Runs until m_running is set to false.
     */
    void workerThreadFunction();

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
     * @brief Calculates chunk priority (distance from player + camera direction)
     *
     * Priority calculation includes:
     * - Distance from player (closer = higher priority)
     * - Camera direction (in front of camera = higher priority)
     * - Vertical distance weight (same level as player = higher priority)
     *
     * @param chunkX Chunk X coordinate
     * @param chunkY Chunk Y coordinate
     * @param chunkZ Chunk Z coordinate
     * @param playerPos Player position
     * @param cameraForward Camera forward direction
     * @return Priority value (lower = higher priority)
     */
    float calculateChunkPriority(int chunkX, int chunkY, int chunkZ,
                                 const glm::vec3& playerPos,
                                 const glm::vec3& cameraForward) const;

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
     * @brief Unloads chunks beyond the unload distance
     *
     * @param playerPos Current player position
     * @param unloadDistance Distance at which to unload chunks
     */
    void unloadDistantChunks(const glm::vec3& playerPos, float unloadDistance);

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
    std::vector<std::unique_ptr<Chunk>> m_completedChunks;  ///< Chunks ready for buffer upload
    mutable std::mutex m_completedMutex;                    ///< Protects m_completedChunks

    // === Player Position ===
    glm::vec3 m_lastPlayerPos;            ///< Last known player position
    glm::vec3 m_lastCameraForward;        ///< Last known camera direction
    mutable std::mutex m_playerPosMutex;  ///< Protects m_lastPlayerPos

    // === Performance Adaptive Loading ===
    std::atomic<float> m_lastFrameTime;   ///< Last frame time in milliseconds
    std::atomic<int> m_maxChunksPerFrame; ///< Adaptive max chunks to process per frame
    static constexpr int MIN_CHUNKS_PER_FRAME = 1;
    static constexpr int MAX_CHUNKS_PER_FRAME = 8;
    static constexpr float TARGET_FRAME_TIME_MS = 16.67f;  ///< Target 60 FPS

    // === Statistics ===
    std::atomic<size_t> m_totalChunksLoaded;    ///< Total chunks loaded since start
    std::atomic<size_t> m_totalChunksUnloaded;  ///< Total chunks unloaded since start
};
