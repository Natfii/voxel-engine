/**
 * @file world_streaming.cpp
 * @brief Implementation of asynchronous chunk streaming system
 *
 * Created: 2025-11-14
 */

#include "world_streaming.h"
#include "world.h"
#include "chunk.h"
#include "vulkan_renderer.h"
#include "biome_map.h"
#include "logger.h"
#include <algorithm>
#include <cmath>

WorldStreaming::WorldStreaming(World* world, BiomeMap* biomeMap, VulkanRenderer* renderer)
    : m_world(world)
    , m_biomeMap(biomeMap)
    , m_renderer(renderer)
    , m_running(false)
    , m_activeWorkers(0)
    , m_meshWorkersRunning(false)
    , m_lastPlayerPos(0.0f, 0.0f, 0.0f)
    , m_lastPlayerChunk(std::numeric_limits<int>::min(), std::numeric_limits<int>::min(), std::numeric_limits<int>::min())
    , m_totalChunksLoaded(0)
    , m_totalChunksUnloaded(0)
{
    Logger::info() << "WorldStreaming initialized";
}

WorldStreaming::~WorldStreaming() {
    // Ensure workers are stopped
    stop();
}

void WorldStreaming::start(int numWorkers) {
    // Don't start if already running
    if (m_running.load()) {
        Logger::warning() << "WorldStreaming already running";
        return;
    }

    // Determine worker count (default: hardware_concurrency - 1, leaving 1 for main thread)
    if (numWorkers <= 0) {
        numWorkers = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
    }

    Logger::info() << "Starting WorldStreaming with " << numWorkers << " worker threads";

    m_running.store(true);
    m_activeWorkers.store(0);

    // Spawn chunk generation worker threads
    for (int i = 0; i < numWorkers; ++i) {
        m_workers.emplace_back(&WorldStreaming::workerThreadFunction, this);
    }

    // PERFORMANCE FIX (2025-11-24): Spawn persistent mesh worker thread pool
    // Eliminates 600+ thread creations/sec from detached thread approach
    const int NUM_MESH_WORKERS = 4;  // 4 workers for mesh generation
    m_meshWorkersRunning.store(true);
    for (int i = 0; i < NUM_MESH_WORKERS; ++i) {
        m_meshWorkers.emplace_back(&WorldStreaming::meshWorkerThreadFunction, this);
    }

    Logger::info() << "WorldStreaming started successfully (" << NUM_MESH_WORKERS << " mesh workers)";
}

void WorldStreaming::stop() {
    if (!m_running.load()) {
        return;  // Already stopped
    }

    Logger::info() << "Stopping WorldStreaming...";

    // Signal workers to exit
    m_running.store(false);

    // Wake up all workers
    m_loadQueueCV.notify_all();

    // Wait for all workers to finish
    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    m_workers.clear();

    // PERFORMANCE FIX (2025-11-24): Shutdown mesh worker thread pool
    m_meshWorkersRunning.store(false);
    m_meshQueueCV.notify_all();  // Wake all mesh workers

    for (auto& meshWorker : m_meshWorkers) {
        if (meshWorker.joinable()) {
            meshWorker.join();
        }
    }

    m_meshWorkers.clear();

    // Clear all pending state to avoid stale entries on restart
    {
        std::lock_guard<std::mutex> lock(m_loadQueueMutex);
        // Clear priority queue by swapping with empty queue
        std::priority_queue<ChunkLoadRequest> emptyQueue;
        m_loadQueue.swap(emptyQueue);
        m_chunksInFlight.clear();
    }

    {
        std::lock_guard<std::mutex> lock(m_completedMutex);
        m_completedChunks.clear();
    }

    {
        std::lock_guard<std::mutex> lock(m_meshQueueMutex);
        std::queue<std::tuple<int, int, int>> emptyMeshQueue;
        m_meshWorkQueue.swap(emptyMeshQueue);
    }

    {
        std::lock_guard<std::mutex> lock(m_chunksMeshingMutex);
        m_chunksBeingMeshed.clear();
    }

    {
        std::lock_guard<std::mutex> lock(m_readyForUploadMutex);
        std::queue<std::tuple<int, int, int>> emptyUploadQueue;
        m_chunksReadyForUpload.swap(emptyUploadQueue);
    }

    {
        std::lock_guard<std::mutex> lock(m_failedChunksMutex);
        m_failedChunks.clear();
    }

    Logger::info() << "WorldStreaming stopped. Total chunks loaded: " << m_totalChunksLoaded.load()
                   << ", unloaded: " << m_totalChunksUnloaded.load();
}

void WorldStreaming::updatePlayerPosition(const glm::vec3& playerPos,
                                          float loadDistance,
                                          float unloadDistance) {
    // Update stored player position
    {
        std::lock_guard<std::mutex> lock(m_playerPosMutex);
        m_lastPlayerPos = playerPos;
    }

    // Convert player position to chunk coordinates
    const int CHUNK_SIZE = 32;  // Chunk::WIDTH
    const float BLOCK_SIZE = 1.0f;  // Blocks are 1.0 world units (not 0.5!)
    int playerChunkX = static_cast<int>(std::floor(playerPos.x / (CHUNK_SIZE * BLOCK_SIZE)));
    int playerChunkY = static_cast<int>(std::floor(playerPos.y / (CHUNK_SIZE * BLOCK_SIZE)));
    int playerChunkZ = static_cast<int>(std::floor(playerPos.z / (CHUNK_SIZE * BLOCK_SIZE)));

    // PERFORMANCE FIX (2025-11-24): Only run expensive streaming operations on chunk boundary crossing
    // Reduces cube iteration from 13,500/sec (3,375 iterations × 4Hz) to ~100/sec (only on boundary cross)
    bool crossedChunkBoundary = false;
    {
        std::lock_guard<std::mutex> lock(m_playerChunkMutex);
        auto currentChunk = std::make_tuple(playerChunkX, playerChunkY, playerChunkZ);
        if (m_lastPlayerChunk != currentChunk) {
            m_lastPlayerChunk = currentChunk;
            crossedChunkBoundary = true;
        }
    }

    // Early exit if player hasn't crossed chunk boundary - nothing to stream!
    if (!crossedChunkBoundary) {
        return;
    }

    // Calculate chunk load radius
    int loadRadiusChunks = static_cast<int>(std::ceil(loadDistance / (CHUNK_SIZE * BLOCK_SIZE)));

    // CRITICAL FIX: Add neighbor margin to ensure chunks at boundary can decorate
    // Chunks need all 4 horizontal neighbors to decorate, but neighbors might be outside loadDistance
    // Add 2 chunk widths (64 blocks) margin to ensure neighbor chunks always load
    const float NEIGHBOR_MARGIN = 2.0f * CHUNK_SIZE * BLOCK_SIZE;  // 64 blocks
    float effectiveLoadDistance = loadDistance + NEIGHBOR_MARGIN;

    // PERFORMANCE FIX: Get loaded chunks AND identify unloads in SINGLE iteration (50% faster!)
    // Previously: Called forEachChunkCoord() twice (once here, once in unloadDistantChunks)
    // Now: Single pass builds hash set AND checks unload distance
    std::unordered_set<ChunkCoord> loadedChunks;
    std::vector<ChunkCoord> chunksToUnload;
    float unloadDistanceSquared = unloadDistance * unloadDistance;

    m_world->forEachChunkCoord([&](const ChunkCoord& coord) {
        loadedChunks.insert(coord);

        // Also check if chunk should be unloaded (avoid second iteration!)
        glm::vec3 chunkCenter = chunkToWorldPos(coord.x, coord.y, coord.z);
        glm::vec3 delta = chunkCenter - playerPos;
        float distanceSquared = glm::dot(delta, delta);

        if (distanceSquared > unloadDistanceSquared) {
            chunksToUnload.push_back(coord);
        }
    });

    // Queue chunks for loading in a sphere around the player
    std::vector<ChunkLoadRequest> newRequests;

    for (int dx = -loadRadiusChunks; dx <= loadRadiusChunks; ++dx) {
        for (int dy = -loadRadiusChunks; dy <= loadRadiusChunks; ++dy) {
            for (int dz = -loadRadiusChunks; dz <= loadRadiusChunks; ++dz) {
                int chunkX = playerChunkX + dx;
                int chunkY = playerChunkY + dy;
                int chunkZ = playerChunkZ + dz;

                // Check if chunk is within load distance (with neighbor margin!)
                if (shouldLoadChunk(chunkX, chunkY, chunkZ, playerPos, effectiveLoadDistance)) {
                    // Check if chunk already exists (O(1) hash lookup, no lock!)
                    ChunkCoord coord{chunkX, chunkY, chunkZ};
                    if (loadedChunks.find(coord) == loadedChunks.end()) {
                        // Calculate priority (distance)
                        float priority = calculateChunkPriority(chunkX, chunkY, chunkZ, playerPos);

                        newRequests.push_back({chunkX, chunkY, chunkZ, priority});
                    }
                }
            }
        }
    }

    // Add new requests to load queue (with deduplication)
    if (!newRequests.empty()) {
        std::lock_guard<std::mutex> lock(m_loadQueueMutex);

        for (const auto& request : newRequests) {
            ChunkCoord coord{request.chunkX, request.chunkY, request.chunkZ};

            // Check if chunk is already in flight (prevents duplicates)
            if (m_chunksInFlight.find(coord) == m_chunksInFlight.end()) {
                m_chunksInFlight.insert(coord);
                m_loadQueue.push(request);
            }
        }

        // Wake up workers if we added any chunks
        if (!m_loadQueue.empty()) {
            m_loadQueueCV.notify_all();
        }
    }

    // Process chunk unloads (already identified in single iteration above)
    // PERFORMANCE FIX: Unload logic now inlined - eliminates second forEachChunkCoord() call
#if USE_INDIRECT_DRAWING
    const int MAX_UNLOADS_PER_CALL = 50;  // High rate - no GPU buffer destruction needed!
#else
    const int MAX_UNLOADS_PER_CALL = 1;  // Ultra-conservative for legacy path (GPU stalls)
#endif
    if (chunksToUnload.size() > MAX_UNLOADS_PER_CALL) {
        chunksToUnload.resize(MAX_UNLOADS_PER_CALL);
    }

    // Batch water cleanup for 50× speedup
    if (!chunksToUnload.empty()) {
        // Convert ChunkCoord to tuple format for batch API
        std::vector<std::tuple<int, int, int>> chunkTuples;
        chunkTuples.reserve(chunksToUnload.size());
        for (const auto& coord : chunksToUnload) {
            chunkTuples.emplace_back(coord.x, coord.y, coord.z);
        }

        // Single batch water cleanup for all chunks (50× faster than individual calls)
        m_world->getWaterSimulation()->notifyChunkUnloadBatch(chunkTuples);
    }

    // Remove marked chunks (water already cleaned up in batch above)
    for (const auto& coord : chunksToUnload) {
        // CRITICAL BUG FIX: Skip chunks being meshed to prevent use-after-free
        {
            std::lock_guard<std::mutex> lock(m_chunksMeshingMutex);
            if (m_chunksBeingMeshed.count(coord) > 0) {
                continue;  // Don't delete while meshing!
            }
        }

        if (m_world->removeChunk(coord.x, coord.y, coord.z, m_renderer, true)) {  // Skip water cleanup
            m_totalChunksUnloaded++;

            // Also remove from in-flight tracking if present
            std::lock_guard<std::mutex> lock(m_loadQueueMutex);
            m_chunksInFlight.erase(coord);
        }
    }

    // Retry failed chunks with exponential backoff
    retryFailedChunks();
}

void WorldStreaming::processCompletedChunks(int maxChunksPerFrame) {
    std::vector<std::unique_ptr<Chunk>> chunksToAdd;

    // Retrieve completed chunks from worker threads
    {
        std::lock_guard<std::mutex> lock(m_completedMutex);

        // Take up to maxChunksPerFrame chunks
        int count = std::min(maxChunksPerFrame, static_cast<int>(m_completedChunks.size()));
        for (int i = 0; i < count; ++i) {
            chunksToAdd.push_back(std::move(m_completedChunks[i]));
        }

        // Remove processed chunks from the queue
        m_completedChunks.erase(m_completedChunks.begin(), m_completedChunks.begin() + count);
    }

    // ============================================================================
    // PHASE 1: Add chunks to world, queue ASYNC mesh generation work
    // ============================================================================
    // CRITICAL PERFORMANCE FIX (2025-11-23): Async mesh generation
    // CRITICAL PERFORMANCE FIX (2025-11-24): Thread pool eliminates 600+ thread creations/sec
    //
    // Architecture:
    //   Main Thread (Frame N):     Add chunks → Push to mesh queue → Return (instant!)
    //   Mesh Worker Threads:       Pull from queue → Generate mesh → Push to ready queue
    //   Main Thread (Frame N+1):   Upload chunks from ready queue to GPU
    //
    // Performance gains:
    //   - Zero main thread blocking (async pipeline)
    //   - Zero thread creation overhead (persistent workers)
    //   - Natural backpressure (queue size limits)
    // ============================================================================

    for (auto& chunk : chunksToAdd) {
        if (chunk) {
            try {
                int chunkX = chunk->getChunkX();
                int chunkY = chunk->getChunkY();
                int chunkZ = chunk->getChunkZ();

                // Add chunk to world with DEFERRED mesh generation AND GPU upload
                bool added = m_world->addStreamedChunk(std::move(chunk), m_renderer, true, true);

                // Remove from in-flight tracking
                {
                    std::lock_guard<std::mutex> lock(m_loadQueueMutex);
                    m_chunksInFlight.erase(ChunkCoord{chunkX, chunkY, chunkZ});
                }

                if (added) {
                    Logger::debug() << "Integrated chunk (" << chunkX << ", " << chunkY << ", " << chunkZ << ")";
                    m_totalChunksLoaded++;

                    // PERFORMANCE FIX (2025-11-24): Use thread pool instead of spawning detached threads
                    // OLD: Spawn 600+ detached threads/sec → massive thread creation overhead
                    // NEW: Push to queue, persistent workers pull → ZERO thread creation overhead

                    // CRITICAL BUG FIX: Track chunk to prevent deletion during meshing
                    {
                        std::lock_guard<std::mutex> lock(m_chunksMeshingMutex);
                        m_chunksBeingMeshed.insert(ChunkCoord{chunkX, chunkY, chunkZ});
                    }

                    // Queue mesh generation work for thread pool
                    {
                        std::lock_guard<std::mutex> lock(m_meshQueueMutex);
                        m_meshWorkQueue.push({chunkX, chunkY, chunkZ});
                    }

                    // Wake one mesh worker to process the work
                    m_meshQueueCV.notify_one();

                } else {
                    Logger::warning() << "Failed to integrate chunk (" << chunkX << ", "
                                  << chunkY << ", " << chunkZ << ") - duplicate or out of bounds";
                }
            } catch (const std::exception& e) {
                int chunkX = chunk ? chunk->getChunkX() : -1;
                int chunkY = chunk ? chunk->getChunkY() : -1;
                int chunkZ = chunk ? chunk->getChunkZ() : -1;

                Logger::error() << "Failed to process chunk (" << chunkX << ", " << chunkY << ", " << chunkZ << "): " << e.what();

                if (chunk) {
                    trackFailedChunk(chunkX, chunkY, chunkZ, std::string("Buffer creation failed: ") + e.what());
                    std::lock_guard<std::mutex> lock(m_loadQueueMutex);
                    m_chunksInFlight.erase(ChunkCoord{chunkX, chunkY, chunkZ});
                }
            }
        }
    }

    // ============================================================================
    // PHASE 2: Upload chunks that finished meshing (from previous frames)
    // ============================================================================
    // PERFORMANCE: Main thread only uploads chunks that are READY (no waiting!)
    // Chunks from background threads queue up here and get uploaded next frame
    // ============================================================================

    std::vector<std::tuple<int, int, int>> chunksToUpload;
    {
        std::lock_guard<std::mutex> lock(m_readyForUploadMutex);

        // Take up to 10 ready chunks (can increase since no blocking!)
        while (!m_chunksReadyForUpload.empty() && chunksToUpload.size() < 10) {
            chunksToUpload.push_back(m_chunksReadyForUpload.front());
            m_chunksReadyForUpload.pop();
        }
    }

    if (!chunksToUpload.empty() && m_renderer) {
        try {
            Logger::info() << "Beginning batched GPU upload for " << chunksToUpload.size() << " chunks";

            m_renderer->beginBatchedChunkUploads();

            for (const auto& [chunkX, chunkY, chunkZ] : chunksToUpload) {
                Chunk* chunkPtr = m_world->getChunkAt(chunkX, chunkY, chunkZ);
                if (chunkPtr) {
                    m_renderer->addChunkToBatch(chunkPtr);
                }
            }

            m_renderer->submitBatchedChunkUploads();

            Logger::info() << "Completed batched GPU upload for " << chunksToUpload.size()
                          << " chunks in single vkQueueSubmit";
        } catch (const std::exception& e) {
            Logger::error() << "Failed batched GPU upload: " << e.what();
        }
    }
}

size_t WorldStreaming::getPendingLoadCount() const {
    std::lock_guard<std::mutex> lock(m_loadQueueMutex);
    return m_loadQueue.size();
}

size_t WorldStreaming::getCompletedChunkCount() const {
    std::lock_guard<std::mutex> lock(m_completedMutex);
    return m_completedChunks.size();
}

std::tuple<size_t, size_t, int> WorldStreaming::getStats() const {
    return std::make_tuple(
        getPendingLoadCount(),
        getCompletedChunkCount(),
        m_activeWorkers.load()
    );
}

void WorldStreaming::workerThreadFunction() {
    m_activeWorkers.fetch_add(1);

    Logger::debug() << "Worker thread started (ID: " << std::this_thread::get_id() << ")";

    while (m_running.load()) {
        ChunkLoadRequest request;
        bool hasWork = false;

        // Wait for work
        {
            std::unique_lock<std::mutex> lock(m_loadQueueMutex);

            // Wait until there's work or we should exit
            m_loadQueueCV.wait(lock, [this]() {
                return !m_loadQueue.empty() || !m_running.load();
            });

            // Exit if shutting down
            if (!m_running.load()) {
                break;
            }

            // Get work item
            if (!m_loadQueue.empty()) {
                request = m_loadQueue.top();
                m_loadQueue.pop();
                hasWork = true;
            }
        }

        // Process chunk generation (outside lock)
        if (hasWork) {
            try {
                // Generate chunk (CPU-only operations)
                auto chunk = generateChunk(request.chunkX, request.chunkY, request.chunkZ);

                // Add to completed queue
                {
                    std::lock_guard<std::mutex> lock(m_completedMutex);
                    m_completedChunks.push_back(std::move(chunk));
                }

                Logger::debug() << "Worker generated chunk (" << request.chunkX << ", "
                               << request.chunkY << ", " << request.chunkZ
                               << ") - Priority: " << request.priority;
            } catch (const std::exception& e) {
                Logger::error() << "Worker failed to generate chunk (" << request.chunkX << ", "
                               << request.chunkY << ", " << request.chunkZ << "): " << e.what();

                // Track failure for potential retry
                trackFailedChunk(request.chunkX, request.chunkY, request.chunkZ, e.what());

                // Remove from in-flight tracking
                {
                    std::lock_guard<std::mutex> lock(m_loadQueueMutex);
                    m_chunksInFlight.erase(ChunkCoord{request.chunkX, request.chunkY, request.chunkZ});
                }
            }
        }
    }

    m_activeWorkers.fetch_sub(1);
    Logger::debug() << "Worker thread exiting (ID: " << std::this_thread::get_id() << ")";
}

void WorldStreaming::meshWorkerThreadFunction() {
    Logger::debug() << "Mesh worker thread started (ID: " << std::this_thread::get_id() << ")";

    while (m_meshWorkersRunning.load()) {
        std::tuple<int, int, int> chunkCoord;
        bool hasWork = false;

        // Wait for work
        {
            std::unique_lock<std::mutex> lock(m_meshQueueMutex);

            // Wait until there's work or we should exit
            m_meshQueueCV.wait(lock, [this]() {
                return !m_meshWorkQueue.empty() || !m_meshWorkersRunning.load();
            });

            // Exit if shutting down
            if (!m_meshWorkersRunning.load()) {
                break;
            }

            // Get work item
            if (!m_meshWorkQueue.empty()) {
                chunkCoord = m_meshWorkQueue.front();
                m_meshWorkQueue.pop();
                hasWork = true;
            }
        }

        // Process mesh generation (outside lock)
        if (hasWork) {
            int chunkX = std::get<0>(chunkCoord);
            int chunkY = std::get<1>(chunkCoord);
            int chunkZ = std::get<2>(chunkCoord);

            try {
                Chunk* chunkPtr = m_world->getChunkAt(chunkX, chunkY, chunkZ);
                if (chunkPtr) {
                    // Generate mesh (CPU-intensive, runs in background)
                    chunkPtr->generateMesh(m_world);

                    // Add to ready queue for GPU upload (next frame)
                    {
                        std::lock_guard<std::mutex> lock(m_readyForUploadMutex);
                        const size_t MAX_QUEUE_SIZE = 100;
                        if (m_chunksReadyForUpload.size() < MAX_QUEUE_SIZE) {
                            m_chunksReadyForUpload.push({chunkX, chunkY, chunkZ});
                        } else {
                            Logger::warning() << "Ready queue full, dropping chunk ("
                                             << chunkX << ", " << chunkY << ", " << chunkZ << ")";
                        }
                    }

                    Logger::debug() << "Mesh generation complete for chunk ("
                                   << chunkX << ", " << chunkY << ", " << chunkZ << ")";
                }
            } catch (const std::exception& e) {
                Logger::error() << "Failed to mesh chunk (" << chunkX << ", "
                              << chunkY << ", " << chunkZ << "): " << e.what();
            }

            // CRITICAL BUG FIX: Remove from tracking set (allow deletion now)
            {
                std::lock_guard<std::mutex> lock(m_chunksMeshingMutex);
                m_chunksBeingMeshed.erase(ChunkCoord{chunkX, chunkY, chunkZ});
            }
        }
    }

    Logger::debug() << "Mesh worker thread exiting (ID: " << std::this_thread::get_id() << ")";
}

std::unique_ptr<Chunk> WorldStreaming::generateChunk(int chunkX, int chunkY, int chunkZ) {
    // PRIORITY 1: Check RAM cache first (10,000x faster than disk!)
    std::unique_ptr<Chunk> chunk = m_world->getChunkFromCache(chunkX, chunkY, chunkZ);
    if (chunk) {
        Logger::debug() << "Loaded chunk (" << chunkX << ", " << chunkY << ", " << chunkZ << ") from RAM cache (instant)";

        // MULTI-STAGE GENERATION FIX (2025-11-24): Cached chunks already have terrain
        chunk->setTerrainReady(true);

        // DON'T generate mesh in worker thread - addStreamedChunk will do it after decoration/lighting
        // Meshing requires neighbors which might not be loaded yet in worker thread
        return chunk;
    }

    // PRIORITY 2: Try to load from disk
    // Use chunk pool for 100x faster allocation!
    chunk = m_world->acquireChunk(chunkX, chunkY, chunkZ);
    bool loadedFromDisk = false;
    if (m_world) {
        std::string worldPath = m_world->getWorldPath();
        if (!worldPath.empty() && chunk->load(worldPath)) {
            loadedFromDisk = true;
            Logger::debug() << "Loaded chunk (" << chunkX << ", " << chunkY << ", " << chunkZ << ") from disk";

            // MULTI-STAGE GENERATION FIX (2025-11-24): Loaded chunks already have terrain
            chunk->setTerrainReady(true);
        }
    }

    // PRIORITY 3: Generate fresh terrain
    if (!loadedFromDisk) {
        chunk->generate(m_biomeMap);
        Logger::debug() << "Generated fresh chunk (" << chunkX << ", " << chunkY << ", " << chunkZ << ")";
    }

    // MULTI-STAGE GENERATION FIX (2025-11-24): Mark terrain as ready (Stage 1 complete)
    // Stage 1: Terrain generation (blocks, heightmap) - DONE
    // Stage 2: Decoration (trees, structures) - will happen later when neighbors are also terrain-ready
    // This prevents deadlock: chunks can now proceed to decoration once all 4 neighbors finish Stage 1
    chunk->setTerrainReady(true);

    // DON'T generate mesh in worker thread - addStreamedChunk will do it after decoration/lighting
    // Meshing requires:
    // 1. Decoration to be complete (so trees/structures are included in lighting calculation)
    // 2. Lighting to be initialized (so vertices have correct light values)
    // 3. Neighbor chunks to exist (for proper face culling and occlusion detection)
    // All of these happen on the main thread, so mesh generation must happen there too!

    return chunk;
}

bool WorldStreaming::shouldLoadChunk(int chunkX, int chunkY, int chunkZ,
                                     const glm::vec3& playerPos, float loadDistance) const {
    glm::vec3 chunkCenter = chunkToWorldPos(chunkX, chunkY, chunkZ);
    float distance = glm::distance(playerPos, chunkCenter);
    return distance <= loadDistance;
}

float WorldStreaming::calculateChunkPriority(int chunkX, int chunkY, int chunkZ,
                                             const glm::vec3& playerPos) const {
    glm::vec3 chunkCenter = chunkToWorldPos(chunkX, chunkY, chunkZ);
    return glm::distance(playerPos, chunkCenter);
}

glm::vec3 WorldStreaming::chunkToWorldPos(int chunkX, int chunkY, int chunkZ) const {
    const int CHUNK_SIZE = 32;  // Chunk::WIDTH
    const float BLOCK_SIZE = 1.0f;  // Blocks are 1.0 world units (not 0.5!)

    // Calculate center of chunk in world space
    float worldX = (chunkX * CHUNK_SIZE + CHUNK_SIZE / 2.0f) * BLOCK_SIZE;
    float worldY = (chunkY * CHUNK_SIZE + CHUNK_SIZE / 2.0f) * BLOCK_SIZE;
    float worldZ = (chunkZ * CHUNK_SIZE + CHUNK_SIZE / 2.0f) * BLOCK_SIZE;

    return glm::vec3(worldX, worldY, worldZ);
}

// REMOVED: unloadDistantChunks() function - logic now inlined in updatePlayerPosition()
// This eliminates double iteration of all chunks (was called via forEachChunkCoord twice per frame)

void WorldStreaming::trackFailedChunk(int chunkX, int chunkY, int chunkZ, const std::string& errorMsg) {
    std::lock_guard<std::mutex> lock(m_failedChunksMutex);

    ChunkCoord coord{chunkX, chunkY, chunkZ};

    // Check if chunk already failed before
    for (auto& failed : m_failedChunks) {
        if (failed.coord.x == chunkX && failed.coord.y == chunkY && failed.coord.z == chunkZ) {
            // Update existing failure
            failed.failureCount++;
            failed.lastAttempt = std::chrono::steady_clock::now();
            failed.errorMessage = errorMsg;

            Logger::warning() << "Chunk (" << chunkX << ", " << chunkY << ", " << chunkZ
                          << ") failed " << failed.failureCount << " times. Last error: " << errorMsg;
            return;
        }
    }

    // New failure
    FailedChunk failed;
    failed.coord = coord;
    failed.failureCount = 1;
    failed.lastAttempt = std::chrono::steady_clock::now();
    failed.errorMessage = errorMsg;
    m_failedChunks.push_back(failed);

    Logger::warning() << "Chunk (" << chunkX << ", " << chunkY << ", " << chunkZ
                  << ") failed to generate: " << errorMsg;
}

void WorldStreaming::retryFailedChunks() {
    std::vector<ChunkLoadRequest> retryRequests;
    std::vector<ChunkCoord> toRemove;

    auto now = std::chrono::steady_clock::now();

    // Check which chunks are ready for retry
    {
        std::lock_guard<std::mutex> lock(m_failedChunksMutex);

        for (auto& failed : m_failedChunks) {
            // Give up after 5 attempts
            if (failed.failureCount >= 5) {
                Logger::error() << "Giving up on chunk (" << failed.coord.x << ", "
                               << failed.coord.y << ", " << failed.coord.z
                               << ") after " << failed.failureCount << " attempts";
                toRemove.push_back(failed.coord);
                continue;
            }

            // Calculate backoff time: 2^failureCount seconds
            auto backoffSeconds = std::chrono::seconds(1 << failed.failureCount);  // 2, 4, 8, 16, 32
            auto nextRetryTime = failed.lastAttempt + backoffSeconds;

            // If enough time has passed, retry
            if (now >= nextRetryTime) {
                // Check if chunk still doesn't exist
                if (m_world->getChunkAt(failed.coord.x, failed.coord.y, failed.coord.z) == nullptr) {
                    float priority = 100.0f;  // Medium priority for retries
                    retryRequests.push_back({failed.coord.x, failed.coord.y, failed.coord.z, priority});

                    Logger::info() << "Retrying failed chunk (" << failed.coord.x << ", "
                                  << failed.coord.y << ", " << failed.coord.z
                                  << ") - Attempt " << (failed.failureCount + 1);
                } else {
                    // Chunk now exists (maybe loaded elsewhere), remove from failed list
                    toRemove.push_back(failed.coord);
                }
            }
        }

        // Remove chunks that gave up or now exist
        m_failedChunks.erase(
            std::remove_if(m_failedChunks.begin(), m_failedChunks.end(),
                [&toRemove](const FailedChunk& f) {
                    return std::find_if(toRemove.begin(), toRemove.end(),
                        [&f](const ChunkCoord& c) {
                            return c.x == f.coord.x && c.y == f.coord.y && c.z == f.coord.z;
                        }) != toRemove.end();
                }),
            m_failedChunks.end()
        );
    }

    // Add retry requests to load queue
    if (!retryRequests.empty()) {
        std::lock_guard<std::mutex> lock(m_loadQueueMutex);

        for (const auto& request : retryRequests) {
            ChunkCoord coord{request.chunkX, request.chunkY, request.chunkZ};

            // Only add if not already in flight
            if (m_chunksInFlight.find(coord) == m_chunksInFlight.end()) {
                m_chunksInFlight.insert(coord);
                m_loadQueue.push(request);
            }
        }

        if (!m_loadQueue.empty()) {
            m_loadQueueCV.notify_all();
        }
    }
}
