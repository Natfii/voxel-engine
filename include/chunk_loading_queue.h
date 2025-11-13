/**
 * @file chunk_loading_queue.h
 * @brief Thread-safe queue architecture for progressive chunk loading
 *
 * Design: Progressive Chunk Loading with Background Generation
 * ============================================================
 *
 * Thread-safe architecture for asynchronous chunk generation without blocking main thread.
 *
 * Main Thread (Rendering)                 Generator Thread(s)
 * =====================                  ====================
 *         |                                       |
 *         |-- 1. Request chunks -------> [INPUT QUEUE]
 *         |                                       |
 *         |                                       |-- 2. Generate terrain
 *         |                                       |-- 3. Generate decoration
 *         |                                       |-- 4. Generate mesh
 *         |                                       |
 *         |<-- 5. Read-only buffers --- [OUTPUT QUEUE]
 *         |
 *         |-- 6. Upload to GPU
 *         |-- 7. Add to rendering pool
 *
 * Key Features:
 * - Main thread never blocks waiting for generation
 * - Non-blocking queue operations for frame rate stability
 * - Generator threads run independently until queue empty
 * - Main thread uploads ready chunks on demand
 */

#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include <cstdint>
#include <thread>
#include <atomic>

// Forward declarations
class Chunk;
class World;
class VulkanRenderer;
class BiomeMap;

/**
 * @brief Chunk work request - identifies which chunk needs generation
 */
struct ChunkRequest {
    int chunkX, chunkY, chunkZ;
    uint64_t requestID;  // Unique ID to track request lifecycle
    uint32_t priority;   // Lower = higher priority (based on distance from player)
};

/**
 * @brief Chunk generation result - completed chunk ready for upload
 *
 * CRITICAL: This struct only contains GENERATED DATA, not references to World.
 * Generator threads CANNOT access World after this is created.
 */
struct GeneratedChunkData {
    int chunkX, chunkY, chunkZ;
    uint64_t requestID;

    // Terrain data (generated, copied from chunk)
    std::vector<int> blockData;           // 32*32*32 block IDs
    std::vector<uint8_t> blockMetadata;   // 32*32*32 metadata

    // Mesh data (generated, ready for GPU upload)
    std::vector<struct Vertex> vertices;     // Opaque vertices
    std::vector<uint32_t> indices;           // Opaque indices
    std::vector<struct Vertex> transparentVertices;  // Transparent vertices
    std::vector<uint32_t> transparentIndices;        // Transparent indices

    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    uint32_t transparentVertexCount = 0;
    uint32_t transparentIndexCount = 0;
};

/**
 * @brief Thread-safe queue for chunk requests
 *
 * Properties:
 * - Multiple readers (generator threads) - NO
 * - Multiple writers (main thread) - YES, but only during frame update
 * - Lock-free NOT feasible (size tracking needed)
 * - Non-blocking necessary for main thread
 */
class ChunkRequestQueue {
public:
    ChunkRequestQueue() = default;

    /**
     * @brief Enqueue a chunk request (main thread -> generator)
     * @param request Chunk generation request
     * @return true if enqueued, false if queue full
     */
    bool enqueue(const ChunkRequest& request);

    /**
     * @brief Dequeue a chunk request (generator thread)
     * @param request Output parameter for dequeued request
     * @return true if dequeued, false if queue empty
     *
     * THREAD-SAFE: Mutex protects queue
     * BLOCKING: Generator thread waits if queue empty (uses condition_variable)
     */
    bool dequeue(ChunkRequest& request, bool blocking = true);

    /**
     * @brief Wait for queue to become non-empty (generator thread sleep)
     * Woken up by enqueue() or shutdown signal
     */
    void waitForWork();

    /**
     * @brief Clear all pending requests
     */
    void clear();

    /**
     * @brief Get approximate size (not exact due to threading)
     */
    size_t size() const;

    /**
     * @brief Signal shutdown to wake all waiting threads
     */
    void signalShutdown();

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<ChunkRequest> m_queue;
    std::atomic<bool> m_shutdown{false};
    static constexpr size_t MAX_QUEUE_SIZE = 512;  // Prevent unbounded growth
};

/**
 * @brief Thread-safe queue for completed chunks
 *
 * Properties:
 * - Multiple readers (main thread) - YES
 * - Multiple writers (generator threads) - YES
 * - Lock-free candidates: Yes, but complexity not worth it
 * - Non-blocking necessary for both directions
 */
class GeneratedChunkQueue {
public:
    GeneratedChunkQueue() = default;

    /**
     * @brief Enqueue generated chunk (generator thread -> main)
     * @param data Completed chunk data
     * @return true if enqueued, false if queue full
     */
    bool enqueue(std::shared_ptr<GeneratedChunkData> data);

    /**
     * @brief Dequeue generated chunk (main thread)
     * @param data Output parameter for dequeued chunk
     * @return true if dequeued, false if queue empty
     */
    bool dequeue(std::shared_ptr<GeneratedChunkData>& data);

    /**
     * @brief Peek at queue size without removing items
     */
    size_t size() const;

    /**
     * @brief Clear all completed chunks
     */
    void clear();

private:
    mutable std::mutex m_mutex;
    std::queue<std::shared_ptr<GeneratedChunkData>> m_queue;
    static constexpr size_t MAX_QUEUE_SIZE = 128;  // Completed chunks, smaller limit
};

/**
 * @brief Background chunk generation manager
 *
 * Responsibilities:
 * 1. Own the generator thread(s)
 * 2. Manage work queues (input and output)
 * 3. Coordinate with World for terrain generation
 * 4. Provide non-blocking interface to main thread
 *
 * Thread Safety Model:
 * ====================
 *
 * GENERATOR THREAD MUST NOT:
 * - Hold mutex across slow operations (generation takes 1-10ms)
 * - Access World::m_chunkMap (causes contention with main thread)
 * - Keep references to chunks after generation
 *
 * GENERATOR THREAD CAN:
 * - Read BiomeMap (thread-safe, constant data)
 * - Read shared noise generator (thread-safe)
 * - Allocate temporary data structures
 *
 * MAIN THREAD:
 * - Submits work via enqueue()
 * - Polls for results via dequeue()
 * - Uploads completed chunks to GPU
 * - Updates World::m_chunkMap atomically
 */
class ChunkLoadingManager {
public:
    ChunkLoadingManager();
    ~ChunkLoadingManager();

    /**
     * @brief Initialize manager with world and renderer
     *
     * @param world World instance (NOT STORED - only used during generation)
     * @param biomeMap Biome map for terrain generation
     * @param renderer Vulkan renderer for GPU uploads
     */
    void initialize(World* world, BiomeMap* biomeMap, VulkanRenderer* renderer);

    /**
     * @brief Start background generator thread(s)
     *
     * @param numThreads Number of generator threads (default: hardware_concurrency / 2)
     */
    void startGenerators(uint32_t numThreads = 0);

    /**
     * @brief Stop generator thread(s) and wait for completion
     *
     * Non-destructive: Can call startGenerators() again later
     */
    void stopGenerators();

    /**
     * @brief Request a chunk for generation
     *
     * @param chunkX, chunkY, chunkZ Chunk coordinates
     * @param priority Generation priority (0 = highest)
     * @return true if request enqueued, false if queue full
     */
    bool requestChunk(int chunkX, int chunkY, int chunkZ, uint32_t priority = 100);

    /**
     * @brief Check for completed chunks and upload to GPU
     *
     * Call once per frame from main thread:
     * - Dequeues completed chunks
     * - Uploads vertex buffers to GPU
     * - Integrates into World
     *
     * @return Number of chunks processed this frame
     */
    uint32_t processCompletedChunks();

    /**
     * @brief Get current load status for debugging
     */
    struct LoadStatus {
        size_t pendingRequests;
        size_t completedChunks;
        uint32_t activeGeneratorThreads;
    };
    LoadStatus getStatus() const;

    /**
     * @brief Get the generated chunk queue (for testing)
     */
    GeneratedChunkQueue& getOutputQueue() { return m_outputQueue; }

private:
    /**
     * @brief Generator thread main loop
     *
     * Pseudocode:
     * while (!shutdown) {
     *     wait_for_work()
     *     while (dequeue(request)) {
     *         generate terrain in temporary chunk
     *         generate mesh
     *         copy data to GeneratedChunkData
     *         enqueue result
     *     }
     * }
     */
    void generatorThreadMain();

    /**
     * @brief Helper: Generate terrain for a single chunk
     * Returns owned chunk data (not stored in World)
     */
    std::shared_ptr<GeneratedChunkData> generateChunk(const ChunkRequest& request);

    // Threading
    std::vector<std::thread> m_generatorThreads;
    std::atomic<bool> m_shutdown{false};
    std::atomic<uint32_t> m_activeThreadCount{0};

    // Work queues
    ChunkRequestQueue m_inputQueue;
    GeneratedChunkQueue m_outputQueue;

    // References (NOT OWNED - provided by main thread)
    World* m_world = nullptr;
    BiomeMap* m_biomeMap = nullptr;
    VulkanRenderer* m_renderer = nullptr;

    // Statistics
    std::atomic<uint64_t> m_nextRequestID{0};
    std::atomic<uint64_t> m_chunksGenerated{0};
    std::atomic<uint64_t> m_chunksUploaded{0};
};

/**
 * @brief THREAD SAFETY PATTERNS
 * ============================
 *
 * Pattern 1: Work Request (Main -> Generator)
 * -------------------------------------------
 * ChunkRequest has atomic ID only
 * No synchronization needed after enqueue
 * Generator owns all data after dequeue
 *
 * Pattern 2: Result Transfer (Generator -> Main)
 * -----------------------------------------------
 * GeneratedChunkData uses shared_ptr
 * Generator releases ownership via enqueue
 * Main thread owns chunk during GPU upload
 * Main thread destroys when complete
 *
 * Pattern 3: World Integration
 * ----------------------------
 * Completed chunk creates NEW Chunk object
 * Main thread inserts into World::m_chunkMap (locked)
 * Generator thread NEVER accesses World after creation
 * Avoids A-B-A problem: gen reads World, main updates, gen reads again
 *
 * Pattern 4: Neighbor Access During Mesh Generation
 * --------------------------------------------------
 * CURRENT (single-threaded):
 *   Chunk::generateMesh(World* world)
 *   -> world->getBlockAt() for neighbors
 *   -> accesses World::m_chunkMap and neighbor Chunk::m_blocks
 *
 * NEW DESIGN (must fix):
 *   Generator gets chunk data ONLY
 *   NO access to World during mesh generation
 *   Mesh generation deferred or uses local copy of neighbor blocks
 *
 * Best approach: Three-phase generation:
 *   Phase 1: Terrain generation (parallel, no neighbor access)
 *   Phase 2: Mesh generation (serial or with edge synchronization)
 *   Phase 3: GPU upload (main thread, sequential)
 *
 * OR: Sync-Point Pattern
 *   All terrain generated first (all chunks available in World)
 *   Then mesh generation can safely read neighbors
 *   Like current system but async with queues
 */
