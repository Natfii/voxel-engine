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
    , m_lastPlayerPos(0.0f, 0.0f, 0.0f)
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

    // Spawn worker threads
    for (int i = 0; i < numWorkers; ++i) {
        m_workers.emplace_back(&WorldStreaming::workerThreadFunction, this);
    }

    Logger::info() << "WorldStreaming started successfully";
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

    // Calculate chunk load radius
    int loadRadiusChunks = static_cast<int>(std::ceil(loadDistance / (CHUNK_SIZE * BLOCK_SIZE)));

    // PERFORMANCE FIX: Get loaded chunks once with ONE lock instead of 1,331 locks!
    // Build a hash set for O(1) existence checks
    std::unordered_set<ChunkCoord> loadedChunks;
    m_world->forEachChunkCoord([&](const ChunkCoord& coord) {
        loadedChunks.insert(coord);
    });

    // Queue chunks for loading in a sphere around the player
    std::vector<ChunkLoadRequest> newRequests;

    for (int dx = -loadRadiusChunks; dx <= loadRadiusChunks; ++dx) {
        for (int dy = -loadRadiusChunks; dy <= loadRadiusChunks; ++dy) {
            for (int dz = -loadRadiusChunks; dz <= loadRadiusChunks; ++dz) {
                int chunkX = playerChunkX + dx;
                int chunkY = playerChunkY + dy;
                int chunkZ = playerChunkZ + dz;

                // Check if chunk is within load distance
                if (shouldLoadChunk(chunkX, chunkY, chunkZ, playerPos, loadDistance)) {
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

    // Unload chunks beyond unload distance
    // Ensure minimum hysteresis to prevent thrashing (at least one chunk width)
    float effectiveUnloadDistance = std::max(unloadDistance, loadDistance + (CHUNK_SIZE * BLOCK_SIZE));
    unloadDistantChunks(playerPos, effectiveUnloadDistance);

    // Retry failed chunks with exponential backoff
    retryFailedChunks();
}

void WorldStreaming::processCompletedChunks(int maxChunksPerFrame) {
    std::vector<std::unique_ptr<Chunk>> chunksToUpload;

    // Retrieve completed chunks
    {
        std::lock_guard<std::mutex> lock(m_completedMutex);

        // Take up to maxChunksPerFrame chunks
        int count = std::min(maxChunksPerFrame, static_cast<int>(m_completedChunks.size()));
        for (int i = 0; i < count; ++i) {
            chunksToUpload.push_back(std::move(m_completedChunks[i]));
        }

        // Remove processed chunks from the queue
        m_completedChunks.erase(m_completedChunks.begin(), m_completedChunks.begin() + count);
    }

    // PHASE 1: Add chunks to world and process decoration/lighting (DEFERRED MESH & GPU UPLOAD)
    // Store chunk coordinates for parallel mesh generation
    std::vector<std::tuple<int, int, int>> addedChunkCoords;

    for (auto& chunk : chunksToUpload) {
        if (chunk) {
            try {
                int chunkX = chunk->getChunkX();
                int chunkY = chunk->getChunkY();
                int chunkZ = chunk->getChunkZ();
                uint32_t vertexCount = chunk->getVertexCount();

                // Add chunk to world with DEFERRED mesh generation AND GPU upload
                // Decoration and lighting happen, but mesh generation is deferred for parallelization
                bool added = m_world->addStreamedChunk(std::move(chunk), m_renderer, true, true);

                // Remove from in-flight tracking
                {
                    std::lock_guard<std::mutex> lock(m_loadQueueMutex);
                    m_chunksInFlight.erase(ChunkCoord{chunkX, chunkY, chunkZ});
                }

                if (added) {
                    Logger::debug() << "Successfully integrated chunk (" << chunkX << ", "
                                   << chunkY << ", " << chunkZ
                                   << ") - Vertices: " << vertexCount;
                    m_totalChunksLoaded++;

                    // Store coordinates for parallel mesh generation
                    addedChunkCoords.push_back({chunkX, chunkY, chunkZ});
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

                    // Remove from in-flight tracking
                    std::lock_guard<std::mutex> lock(m_loadQueueMutex);
                    m_chunksInFlight.erase(ChunkCoord{chunkX, chunkY, chunkZ});
                }
            }
        }
    }

    // PHASE 2: PARALLEL MESH GENERATION (all chunks meshed simultaneously)
    // OPTIMIZATION (2025-11-23): Mesh generation is CPU-intensive, parallelize for huge speedup
    // World::getBlockAt uses shared_lock, allowing concurrent reads from multiple threads
    if (!addedChunkCoords.empty()) {
        try {
            Logger::info() << "Beginning parallel mesh generation for " << addedChunkCoords.size() << " chunks";

            std::vector<std::thread> threads;
            std::mutex errorMutex;
            std::vector<std::string> errors;

            for (const auto& [chunkX, chunkY, chunkZ] : addedChunkCoords) {
                threads.emplace_back([this, chunkX, chunkY, chunkZ, &errorMutex, &errors]() {
                    try {
                        Chunk* chunkPtr = m_world->getChunkAt(chunkX, chunkY, chunkZ);
                        if (chunkPtr) {
                            chunkPtr->generateMesh(m_world);
                        }
                    } catch (const std::exception& e) {
                        std::lock_guard<std::mutex> lock(errorMutex);
                        std::ostringstream oss;
                        oss << "Chunk (" << chunkX << ", " << chunkY << ", " << chunkZ << "): " << e.what();
                        errors.push_back(oss.str());
                    }
                });
            }

            // Wait for all mesh generation to complete
            for (auto& thread : threads) {
                thread.join();
            }

            // Log any errors
            for (const auto& error : errors) {
                Logger::error() << "Failed to mesh chunk: " << error;
            }

            Logger::info() << "Completed parallel mesh generation for " << addedChunkCoords.size() << " chunks";
        } catch (const std::exception& e) {
            Logger::error() << "Failed parallel mesh generation: " << e.what();
        }
    }

    // PHASE 3: BATCHED GPU UPLOAD (all chunks in ONE vkQueueSubmit)
    // OPTIMIZATION (2025-11-23): Batch all chunk uploads into single GPU submission
    // Reduces vkQueueSubmit overhead from N calls to 1 call (90% reduction for 10 chunks)
    if (!addedChunkCoords.empty() && m_renderer) {
        try {
            Logger::info() << "Beginning batched GPU upload for " << addedChunkCoords.size() << " chunks";

            m_renderer->beginBatchedChunkUploads();

            for (const auto& [chunkX, chunkY, chunkZ] : addedChunkCoords) {
                Chunk* chunkPtr = m_world->getChunkAt(chunkX, chunkY, chunkZ);
                if (chunkPtr) {
                    m_renderer->addChunkToBatch(chunkPtr);
                }
            }

            m_renderer->submitBatchedChunkUploads();

            Logger::info() << "Completed batched GPU upload for " << addedChunkCoords.size()
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

std::unique_ptr<Chunk> WorldStreaming::generateChunk(int chunkX, int chunkY, int chunkZ) {
    // PRIORITY 1: Check RAM cache first (10,000x faster than disk!)
    std::unique_ptr<Chunk> chunk = m_world->getChunkFromCache(chunkX, chunkY, chunkZ);
    if (chunk) {
        Logger::debug() << "Loaded chunk (" << chunkX << ", " << chunkY << ", " << chunkZ << ") from RAM cache (instant)";

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
        }
    }

    // PRIORITY 3: Generate fresh terrain
    if (!loadedFromDisk) {
        chunk->generate(m_biomeMap);
        Logger::debug() << "Generated fresh chunk (" << chunkX << ", " << chunkY << ", " << chunkZ << ")";
    }

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

void WorldStreaming::unloadDistantChunks(const glm::vec3& playerPos, float unloadDistance) {
    const int CHUNK_SIZE = 32;
    const float BLOCK_SIZE = 1.0f;  // Blocks are 1.0 world units (not 0.5!)

    std::vector<ChunkCoord> chunksToUnload;
    float unloadDistanceSquared = unloadDistance * unloadDistance;

    // PERFORMANCE FIX: Use zero-copy callback iteration instead of copying 432 coords
    // Reduces "stream" time from 75-118ms to <10ms
    m_world->forEachChunkCoord([&](const ChunkCoord& coord) {
        glm::vec3 chunkCenter = chunkToWorldPos(coord.x, coord.y, coord.z);
        glm::vec3 delta = chunkCenter - playerPos;
        float distanceSquared = glm::dot(delta, delta);

        // If beyond unload distance, mark for removal
        if (distanceSquared > unloadDistanceSquared) {
            chunksToUnload.push_back(coord);
        }
    });

    // PERFORMANCE FIX (2025-11-23): Re-enable chunk unloading with ultra-conservative rate
    // Previous: DISABLED (0) - caused infinite memory accumulation and frame time increase
    // GPU buffer deletion causes MASSIVE stalls (1682ms for 5 chunks!)
    //
    // UPDATE: With indirect drawing, chunks no longer have individual GPU buffers!
    // They only write to mega-buffers, so unloading is nearly instant.
#if USE_INDIRECT_DRAWING
    const int MAX_UNLOADS_PER_CALL = 50;  // High rate - no GPU buffer destruction needed!
#else
    const int MAX_UNLOADS_PER_CALL = 1;  // Ultra-conservative for legacy path (GPU stalls)
#endif
    if (chunksToUnload.size() > MAX_UNLOADS_PER_CALL) {
        chunksToUnload.resize(MAX_UNLOADS_PER_CALL);
    }

    // Remove marked chunks
    for (const auto& coord : chunksToUnload) {
        if (m_world->removeChunk(coord.x, coord.y, coord.z, m_renderer)) {
            Logger::debug() << "Unloaded distant chunk (" << coord.x << ", " << coord.y << ", " << coord.z << ")";
            m_totalChunksUnloaded++;

            // Also remove from in-flight tracking if present
            std::lock_guard<std::mutex> lock(m_loadQueueMutex);
            m_chunksInFlight.erase(coord);
        }
    }

    if (!chunksToUnload.empty()) {
        Logger::info() << "Unloaded " << chunksToUnload.size() << " distant chunks";
    }
}

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
