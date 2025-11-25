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
    , m_previousPlayerPos(0.0f, 0.0f, 0.0f)
    , m_lastVelocityUpdate(std::chrono::high_resolution_clock::now())
    , m_playerVelocity(0.0f)
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
    // Calculate player velocity and adjust load distance if moving fast
    float velocity = 0.0f;
    {
        std::lock_guard<std::mutex> lock(m_playerPosMutex);
        auto now = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(now - m_lastVelocityUpdate).count();

        if (deltaTime > 0.0f) {
            // Calculate velocity (blocks per second)
            glm::vec3 delta = playerPos - m_previousPlayerPos;
            float distance = glm::length(delta);
            velocity = distance / deltaTime;

            m_playerVelocity = velocity;
            m_previousPlayerPos = m_lastPlayerPos;
            m_lastVelocityUpdate = now;
        }

        m_lastPlayerPos = playerPos;
    }

    // PERFORMANCE: Defer distant chunk generation when moving fast
    // When sprinting/flying (>20 blocks/sec), only load closer chunks to reduce lag
    const float FAST_MOVEMENT_THRESHOLD = 20.0f;  // Sprinting speed
    if (velocity > FAST_MOVEMENT_THRESHOLD) {
        // Reduce load distance when moving fast (prioritize chunks in view)
        float speedFactor = std::min(velocity / FAST_MOVEMENT_THRESHOLD, 2.0f);
        loadDistance = loadDistance / speedFactor;  // Halve load distance at 2x sprint speed
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

    // BUG FIX (2025-11-24): Calculate effective radius in CHUNKS for iteration bounds
    // Previously: Iterated loadRadiusChunks but checked effectiveLoadDistance → margin chunks never checked!
    // This caused 169 chunks to never get neighbors → performance death spiral
    int effectiveLoadRadiusChunks = static_cast<int>(std::ceil(effectiveLoadDistance / (CHUNK_SIZE * BLOCK_SIZE)));

    // PERFORMANCE FIX: Get loaded chunks AND identify unloads in SINGLE iteration (50% faster!)
    // Previously: Called forEachChunkCoord() twice (once here, once in unloadDistantChunks)
    // Now: Single pass builds hash set AND checks unload distance
    std::unordered_set<ChunkCoord> loadedChunks;
    std::vector<ChunkCoord> chunksToUnload;
    float unloadDistanceSquared = unloadDistance * unloadDistance;

    m_world->forEachChunkCoord([&](const ChunkCoord& coord) {
        loadedChunks.insert(coord);

        // Also check if chunk should be unloaded (avoid second iteration!)
        // SPAWN ANCHOR (2025-11-25): Never unload chunks within spawn anchor radius
        if (isInSpawnAnchor(coord.x, coord.y, coord.z)) {
            return;  // Skip - spawn chunks stay loaded permanently
        }

        glm::vec3 chunkCenter = chunkToWorldPos(coord.x, coord.y, coord.z);
        glm::vec3 delta = chunkCenter - playerPos;
        float distanceSquared = glm::dot(delta, delta);

        if (distanceSquared > unloadDistanceSquared) {
            chunksToUnload.push_back(coord);
        }
    });

    // Queue chunks for loading in a sphere around the player
    std::vector<ChunkLoadRequest> newRequests;

    // BUG FIX (2025-11-24): Iterate effectiveLoadRadiusChunks (includes margin!) not loadRadiusChunks
    // This ensures we check ALL chunks within effectiveLoadDistance, not just the inner radius
    for (int dx = -effectiveLoadRadiusChunks; dx <= effectiveLoadRadiusChunks; ++dx) {
        for (int dy = -effectiveLoadRadiusChunks; dy <= effectiveLoadRadiusChunks; ++dy) {
            for (int dz = -effectiveLoadRadiusChunks; dz <= effectiveLoadRadiusChunks; ++dz) {
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

                        // LOD TIER (2025-11-25): Determine detail level based on distance
                        // - FULL (0-48 blocks): Full decoration + mesh (trees visible)
                        // - MESH_ONLY (48-80 blocks): Mesh only, skip decoration (fog hides trees)
                        // - TERRAIN_ONLY (>80 blocks): No mesh, no decoration (beyond render)
                        ChunkLOD lod = ChunkLOD::FULL;
                        if (priority > loadDistance) {
                            lod = ChunkLOD::TERRAIN_ONLY;  // Beyond render distance
                        } else if (priority > loadDistance * 0.6f) {
                            lod = ChunkLOD::MESH_ONLY;     // In fog zone, skip decoration
                        }

                        newRequests.push_back({chunkX, chunkY, chunkZ, priority, lod});
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

    // ============================================================================
    // PREDICTIVE PRE-GENERATION (2025-11-25): Generate chunks ahead of movement
    // ============================================================================
    // When player is moving, pre-generate chunks in their movement direction.
    // This reduces pop-in when moving quickly through the world.
    // ============================================================================
    if (m_predictiveEnabled && m_playerVelocity > 2.0f) {
        // Calculate movement direction
        glm::vec3 delta = playerPos - m_previousPlayerPos;
        if (glm::length(delta) > 0.1f) {
            m_playerMovementDir = glm::normalize(delta);

            // Calculate look-ahead position (where player will be)
            glm::vec3 lookAheadPos = playerPos + m_playerMovementDir * m_lookAheadDistance;

            // Convert to chunk coordinates
            int lookAheadChunkX = static_cast<int>(std::floor(lookAheadPos.x / 32.0f));
            int lookAheadChunkZ = static_cast<int>(std::floor(lookAheadPos.z / 32.0f));

            // Queue chunks around the look-ahead position (but don't spam)
            static int lastLookAheadX = INT_MAX, lastLookAheadZ = INT_MAX;
            if (lookAheadChunkX != lastLookAheadX || lookAheadChunkZ != lastLookAheadZ) {
                lastLookAheadX = lookAheadChunkX;
                lastLookAheadZ = lookAheadChunkZ;

                // Queue a small radius around the look-ahead point
                const int LOOK_AHEAD_RADIUS = 2;  // Small radius (5x5 chunks)
                for (int dz = -LOOK_AHEAD_RADIUS; dz <= LOOK_AHEAD_RADIUS; dz++) {
                    for (int dx = -LOOK_AHEAD_RADIUS; dx <= LOOK_AHEAD_RADIUS; dx++) {
                        for (int chunkY = 1; chunkY <= 3; chunkY++) {
                            int chunkX = lookAheadChunkX + dx;
                            int chunkZ = lookAheadChunkZ + dz;

                            ChunkCoord coord{chunkX, chunkY, chunkZ};
                            if (loadedChunks.find(coord) == loadedChunks.end()) {
                                std::lock_guard<std::mutex> lock(m_loadQueueMutex);
                                if (m_chunksInFlight.find(coord) == m_chunksInFlight.end()) {
                                    m_chunksInFlight.insert(coord);

                                    ChunkLoadRequest request;
                                    request.chunkX = chunkX;
                                    request.chunkY = chunkY;
                                    request.chunkZ = chunkZ;
                                    request.priority = 50.0f;  // High priority for look-ahead
                                    request.lod = ChunkLOD::FULL;

                                    m_loadQueue.push(request);
                                }
                            }
                        }
                    }
                }

                m_loadQueueCV.notify_all();
            }
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

void WorldStreaming::processCompletedChunks(int maxChunksPerFrame, float maxMilliseconds) {
    auto startTime = std::chrono::high_resolution_clock::now();

    std::vector<CompletedChunk> chunksToAdd;

    // Retrieve completed chunks from worker threads (with LOD)
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
    // LOD TIER FIX (2025-11-25): Skip decoration/mesh for distant chunks
    //
    // Architecture:
    //   Main Thread (Frame N):     Add chunks → Push to mesh queue → Return (instant!)
    //   Mesh Worker Threads:       Pull from queue → Generate mesh → Push to ready queue
    //   Main Thread (Frame N+1):   Upload chunks from ready queue to GPU
    //
    // LOD Tiers:
    //   FULL: Close chunks - decoration + mesh (visible)
    //   MESH_ONLY: Medium chunks - mesh only, skip decoration (fog hides trees)
    //   TERRAIN_ONLY: Far chunks - no mesh, no decoration (beyond render distance)
    // ============================================================================

    for (auto& completed : chunksToAdd) {
        if (completed.chunk) {
            try {
                int chunkX = completed.chunk->getChunkX();
                int chunkY = completed.chunk->getChunkY();
                int chunkZ = completed.chunk->getChunkZ();
                ChunkLOD lod = completed.lod;

                // Add chunk to world with DEFERRED mesh generation AND GPU upload
                // LOD determines whether decoration/mesh is skipped
                bool added = m_world->addStreamedChunk(std::move(completed.chunk), m_renderer, true, true, lod);

                // Remove from in-flight tracking
                {
                    std::lock_guard<std::mutex> lock(m_loadQueueMutex);
                    m_chunksInFlight.erase(ChunkCoord{chunkX, chunkY, chunkZ});
                }

                if (added) {
                    Logger::debug() << "Integrated chunk (" << chunkX << ", " << chunkY << ", " << chunkZ
                                   << ") LOD=" << static_cast<int>(lod);
                    m_totalChunksLoaded++;

                    // LOD TIER (2025-11-25): Only queue mesh for visible chunks
                    // TERRAIN_ONLY chunks are beyond render distance - no mesh needed!
                    if (lod != ChunkLOD::TERRAIN_ONLY) {
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
                        Logger::debug() << "Skipping mesh for far chunk (" << chunkX << ", " << chunkY << ", " << chunkZ << ") - TERRAIN_ONLY";
                    }

                } else {
                    Logger::warning() << "Failed to integrate chunk (" << chunkX << ", "
                                  << chunkY << ", " << chunkZ << ") - duplicate or out of bounds";
                }
            } catch (const std::exception& e) {
                int chunkX = completed.chunk ? completed.chunk->getChunkX() : -1;
                int chunkY = completed.chunk ? completed.chunk->getChunkY() : -1;
                int chunkZ = completed.chunk ? completed.chunk->getChunkZ() : -1;

                Logger::error() << "Failed to process chunk (" << chunkX << ", " << chunkY << ", " << chunkZ << "): " << e.what();

                if (completed.chunk) {
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

    // ============================================================================
    // FRAME BUDGET CHECK: Stop if we've exceeded our time allocation
    // ============================================================================
    if (maxMilliseconds > 0.0f) {
        auto now = std::chrono::high_resolution_clock::now();
        float elapsed = std::chrono::duration<float, std::milli>(now - startTime).count();
        if (elapsed >= maxMilliseconds) {
            // Exceeded budget - defer remaining work to next frame
            return;
        }
    }

    std::vector<std::tuple<int, int, int>> chunksToUpload;
    {
        std::lock_guard<std::mutex> lock(m_readyForUploadMutex);

        // ============================================================================
        // GPU BACKLOG-AWARE UPLOAD (2025-11-25)
        // ============================================================================
        // Instead of fixed "1 chunk per frame", adapt to GPU state:
        // - Check GPU backlog before uploading
        // - Use recommended count based on pending uploads
        // - Skip entirely if severely backlogged (prevents 1000ms+ stalls)
        // ============================================================================
        size_t maxUploads = m_renderer ? m_renderer->getRecommendedUploadCount() : 1;

        // Log backlog state periodically for debugging
        static int backlogLogCounter = 0;
        if (m_renderer && ++backlogLogCounter % 300 == 0) {  // Every 5 sec at 60fps
            size_t pending = m_renderer->getPendingUploadCount();
            if (pending > 5) {
                Logger::debug() << "GPU upload backlog: " << pending
                               << " pending, recommended: " << maxUploads << " uploads/frame";
            }
        }

        // Collect chunks respecting GPU backlog limit
        while (!m_chunksReadyForUpload.empty() && chunksToUpload.size() < maxUploads) {
            // Check time budget before adding more chunks
            if (maxMilliseconds > 0.0f) {
                auto now = std::chrono::high_resolution_clock::now();
                float elapsed = std::chrono::duration<float, std::milli>(now - startTime).count();
                if (elapsed >= maxMilliseconds * 0.8f) {  // Reserve 20% for upload
                    break;
                }
            }

            chunksToUpload.push_back(m_chunksReadyForUpload.front());
            m_chunksReadyForUpload.pop();
        }

        // ============================================================================
        // PRIORITY SORT: Upload visible/close chunks first
        // ============================================================================
        if (chunksToUpload.size() > 1) {
            glm::vec3 playerPos = m_lastPlayerPos;
            std::sort(chunksToUpload.begin(), chunksToUpload.end(),
                [&playerPos](const auto& a, const auto& b) {
                    // Calculate distance to player for each chunk
                    auto [ax, ay, az] = a;
                    auto [bx, by, bz] = b;

                    float distA = glm::length(glm::vec3(ax * 32, ay * 32, az * 32) - playerPos);
                    float distB = glm::length(glm::vec3(bx * 32, by * 32, bz * 32) - playerPos);

                    // Prioritize surface chunks (y >= 0) over underground
                    bool surfaceA = ay >= 0;
                    bool surfaceB = by >= 0;
                    if (surfaceA != surfaceB) return surfaceA;  // Surface first

                    return distA < distB;  // Then by distance
                });
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

                // Check time budget during upload loop
                if (maxMilliseconds > 0.0f) {
                    auto now = std::chrono::high_resolution_clock::now();
                    float elapsed = std::chrono::duration<float, std::milli>(now - startTime).count();
                    if (elapsed >= maxMilliseconds) {
                        // Exceeded budget - submit what we have so far
                        break;
                    }
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

void WorldStreaming::queueChunkForMeshing(int chunkX, int chunkY, int chunkZ) {
    // PERFORMANCE FIX (2025-11-25): Allow decoration system to use async mesh pipeline
    // This prevents 4+ second frame stalls from sync mesh generation in processPendingDecorations

    // Track chunk to prevent deletion during meshing
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

    Logger::debug() << "Queued decorated chunk (" << chunkX << ", " << chunkY << ", " << chunkZ << ") for async meshing";
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

                // Add to completed queue (with LOD tier)
                {
                    std::lock_guard<std::mutex> lock(m_completedMutex);
                    m_completedChunks.push_back({std::move(chunk), request.lod});
                }

                Logger::debug() << "Worker generated chunk (" << request.chunkX << ", "
                               << request.chunkY << ", " << request.chunkZ
                               << ") - Priority: " << request.priority
                               << ", LOD: " << static_cast<int>(request.lod);
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

            // PERFORMANCE FIX (2025-11-24): Backpressure to prevent queue overflow
            // Check upload queue occupancy BEFORE expensive mesh generation
            // If queue is nearly full, wait for GPU to catch up (prevents wasted work)
            const size_t MAX_QUEUE_SIZE = 100;
            const size_t THROTTLE_THRESHOLD = 85;  // Start throttling at 85% capacity (raised from 75%)
            bool shouldThrottle = false;

            {
                std::lock_guard<std::mutex> lock(m_readyForUploadMutex);
                shouldThrottle = m_chunksReadyForUpload.size() >= THROTTLE_THRESHOLD;
            }

            if (shouldThrottle) {
                // Queue nearly full - wait for GPU upload to catch up
                // This prevents wasted mesh generation that would be dropped
                std::this_thread::sleep_for(std::chrono::milliseconds(10));

                // Put work back in queue for retry
                // NOTE: Keep chunk in m_chunksBeingMeshed tracking set - it's still in the pipeline
                {
                    std::lock_guard<std::mutex> lock(m_meshQueueMutex);
                    m_meshWorkQueue.push({chunkX, chunkY, chunkZ});
                }

                continue;  // Skip to next iteration
            }

            try {
                Chunk* chunkPtr = m_world->getChunkAt(chunkX, chunkY, chunkZ);
                if (chunkPtr) {
                    // ============================================================================
                    // OCCLUSION SKIP (2025-11-25): Skip mesh generation for fully occluded chunks
                    // ============================================================================
                    // Underground chunks surrounded by solid blocks are invisible.
                    // Skip expensive mesh generation entirely - saves ~1-2ms per chunk!
                    // Check only for underground chunks (Y < 0) to avoid surface pop-in.
                    // ============================================================================
                    if (chunkY < 0 && chunkPtr->isFullyOccluded(m_world, false)) {
                        Logger::debug() << "Skipping occluded underground chunk ("
                                       << chunkX << ", " << chunkY << ", " << chunkZ << ")";

                        // Remove from tracking - this chunk doesn't need meshing
                        {
                            std::lock_guard<std::mutex> lock(m_chunksMeshingMutex);
                            m_chunksBeingMeshed.erase(ChunkCoord{chunkX, chunkY, chunkZ});
                        }

                        continue;  // Skip to next chunk
                    }

                    // ASYNC LIGHTING (2025-11-25): Initialize lighting on worker thread
                    // This is now safe because:
                    // 1. If no emissive blocks exist (common case), this is instant
                    // 2. If emissive blocks exist, we only scan for those specific IDs
                    // 3. Lighting system's addLightSource is thread-safe
                    if (!chunkPtr->hasLightingData()) {
                        m_world->initializeChunkLighting(chunkPtr);
                    }

                    // Generate mesh (CPU-intensive, runs in background)
                    chunkPtr->generateMesh(m_world, false, 0);

                    // Add to ready queue for GPU upload (next frame)
                    {
                        std::lock_guard<std::mutex> lock(m_readyForUploadMutex);
                        if (m_chunksReadyForUpload.size() < MAX_QUEUE_SIZE) {
                            m_chunksReadyForUpload.push({chunkX, chunkY, chunkZ});
                        } else {
                            // Should rarely happen now due to throttling above
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
    float distance = glm::distance(playerPos, chunkCenter);

    // PERFORMANCE OPTIMIZATION (2025-11-24): Quadratic priority
    // Makes nearby chunks WAY higher priority than distant ones
    // Linear: chunk at distance 10 is 2x priority of distance 20
    // Quadratic: chunk at distance 10 is 4x priority of distance 20
    // Result: Dramatically reduces time spent on distant chunks
    return distance * distance;  // Squared distance = quadratic priority
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

// ========== Spawn Anchor (Minecraft-style spawn chunks) ==========

void WorldStreaming::setSpawnAnchor(int chunkX, int chunkY, int chunkZ, int radius) {
    m_spawnAnchorX = chunkX;
    m_spawnAnchorY = chunkY;
    m_spawnAnchorZ = chunkZ;
    m_spawnAnchorRadius = radius;
    m_spawnAnchorEnabled = true;

    Logger::info() << "Spawn anchor set at chunk (" << chunkX << ", " << chunkY << ", " << chunkZ
                   << ") with radius " << radius << " (" << ((2*radius+1)*(2*radius+1)*(2*radius+1)) << " chunks)";
}

bool WorldStreaming::isInSpawnAnchor(int chunkX, int chunkY, int chunkZ) const {
    if (!m_spawnAnchorEnabled) return false;

    // Check if chunk is within spawn anchor radius (cube, not sphere)
    int dx = std::abs(chunkX - m_spawnAnchorX);
    int dy = std::abs(chunkY - m_spawnAnchorY);
    int dz = std::abs(chunkZ - m_spawnAnchorZ);

    return dx <= m_spawnAnchorRadius && dy <= m_spawnAnchorRadius && dz <= m_spawnAnchorRadius;
}

// ========== Background Pre-Generation (2025-11-25) ==========

void WorldStreaming::setPredictiveGeneration(bool enabled, float lookAheadDistance) {
    m_predictiveEnabled = enabled;
    m_lookAheadDistance = lookAheadDistance;

    Logger::info() << "Predictive generation " << (enabled ? "enabled" : "disabled")
                  << " (look-ahead: " << lookAheadDistance << " blocks)";
}

void WorldStreaming::queueBackgroundGeneration(int centerX, int centerZ, int radius) {
    if (!m_running.load()) return;

    // Convert world coordinates to chunk coordinates
    constexpr int CHUNK_SIZE = 32;
    int centerChunkX = centerX / CHUNK_SIZE;
    int centerChunkZ = centerZ / CHUNK_SIZE;

    // Queue chunks at surface level (Y=2 is surface, also Y=1 and Y=3 for caves/hills)
    std::vector<ChunkLoadRequest> backgroundRequests;

    for (int dz = -radius; dz <= radius; dz++) {
        for (int dx = -radius; dx <= radius; dx++) {
            // Skip corners (circular pattern)
            if (dx*dx + dz*dz > radius*radius) continue;

            int chunkX = centerChunkX + dx;
            int chunkZ = centerChunkZ + dz;

            // Queue surface and near-surface chunks
            for (int chunkY = 1; chunkY <= 3; chunkY++) {
                // Skip if already loaded or in flight
                if (m_world->getChunkAt(chunkX, chunkY, chunkZ) != nullptr) continue;

                ChunkCoord coord{chunkX, chunkY, chunkZ};
                {
                    std::lock_guard<std::mutex> lock(m_loadQueueMutex);
                    if (m_chunksInFlight.find(coord) != m_chunksInFlight.end()) continue;
                }

                // Use distance as priority (further = lower priority)
                float distance = static_cast<float>(std::sqrt(dx*dx + dz*dz)) * CHUNK_SIZE;

                ChunkLoadRequest request;
                request.chunkX = chunkX;
                request.chunkY = chunkY;
                request.chunkZ = chunkZ;
                request.priority = distance + 1000.0f;  // Add 1000 to deprioritize vs player-near chunks
                request.lod = ChunkLOD::TERRAIN_ONLY;   // Just terrain, no mesh (low priority)

                backgroundRequests.push_back(request);
            }
        }
    }

    // Add to load queue
    if (!backgroundRequests.empty()) {
        std::lock_guard<std::mutex> lock(m_loadQueueMutex);
        for (const auto& request : backgroundRequests) {
            ChunkCoord coord{request.chunkX, request.chunkY, request.chunkZ};
            if (m_chunksInFlight.find(coord) == m_chunksInFlight.end()) {
                m_chunksInFlight.insert(coord);
                m_loadQueue.push(request);
            }
        }

        if (!m_loadQueue.empty()) {
            m_loadQueueCV.notify_all();
        }

        Logger::debug() << "Queued " << backgroundRequests.size()
                       << " chunks for background pre-generation";
    }
}
