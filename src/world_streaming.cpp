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
    const float BLOCK_SIZE = 0.5f;
    int playerChunkX = static_cast<int>(std::floor(playerPos.x / (CHUNK_SIZE * BLOCK_SIZE)));
    int playerChunkY = static_cast<int>(std::floor(playerPos.y / (CHUNK_SIZE * BLOCK_SIZE)));
    int playerChunkZ = static_cast<int>(std::floor(playerPos.z / (CHUNK_SIZE * BLOCK_SIZE)));

    // Calculate chunk load radius
    int loadRadiusChunks = static_cast<int>(std::ceil(loadDistance / (CHUNK_SIZE * BLOCK_SIZE)));

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
                    // Check if chunk already exists
                    if (m_world->getChunkAt(chunkX, chunkY, chunkZ) == nullptr) {
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
    if (unloadDistance > loadDistance) {
        unloadDistantChunks(playerPos, unloadDistance);
    }

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

    // Upload to GPU (main thread only - Vulkan not thread-safe)
    for (auto& chunk : chunksToUpload) {
        if (chunk) {
            try {
                // Create Vulkan buffers for the chunk
                chunk->createVertexBuffer(m_renderer);

                // Add chunk to world's chunk map
                int chunkX = chunk->getChunkX();
                int chunkY = chunk->getChunkY();
                int chunkZ = chunk->getChunkZ();
                uint32_t vertexCount = chunk->getVertexCount();

                bool added = m_world->addStreamedChunk(std::move(chunk));

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
                } else {
                    Logger::warning() << "Failed to integrate chunk (" << chunkX << ", "
                                  << chunkY << ", " << chunkZ << ") - duplicate or out of bounds";
                }
            } catch (const std::exception& e) {
                // Track the failure for retry
                int chunkX = chunk ? chunk->getChunkX() : -1;
                int chunkY = chunk ? chunk->getChunkY() : -1;
                int chunkZ = chunk ? chunk->getChunkZ() : -1;

                Logger::error() << "Failed to upload chunk (" << chunkX << ", " << chunkY << ", " << chunkZ << "): " << e.what();

                if (chunk) {
                    trackFailedChunk(chunkX, chunkY, chunkZ, std::string("Buffer creation failed: ") + e.what());

                    // Remove from in-flight tracking
                    std::lock_guard<std::mutex> lock(m_loadQueueMutex);
                    m_chunksInFlight.erase(ChunkCoord{chunkX, chunkY, chunkZ});
                }
            }
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
    m_activeWorkers++;

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

    m_activeWorkers--;
    Logger::debug() << "Worker thread exiting (ID: " << std::this_thread::get_id() << ")";
}

std::unique_ptr<Chunk> WorldStreaming::generateChunk(int chunkX, int chunkY, int chunkZ) {
    // Create chunk
    auto chunk = std::make_unique<Chunk>(chunkX, chunkY, chunkZ);

    // Generate terrain
    chunk->generate(m_biomeMap);

    // Generate mesh (CPU-only, thread-safe with thread-local pools)
    chunk->generateMesh(m_world);

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
    const float BLOCK_SIZE = 0.5f;

    // Calculate center of chunk in world space
    float worldX = (chunkX * CHUNK_SIZE + CHUNK_SIZE / 2.0f) * BLOCK_SIZE;
    float worldY = (chunkY * CHUNK_SIZE + CHUNK_SIZE / 2.0f) * BLOCK_SIZE;
    float worldZ = (chunkZ * CHUNK_SIZE + CHUNK_SIZE / 2.0f) * BLOCK_SIZE;

    return glm::vec3(worldX, worldY, worldZ);
}

void WorldStreaming::unloadDistantChunks(const glm::vec3& playerPos, float unloadDistance) {
    const int CHUNK_SIZE = 32;
    const float BLOCK_SIZE = 0.5f;

    // Convert player position to chunk coordinates
    int playerChunkX = static_cast<int>(std::floor(playerPos.x / (CHUNK_SIZE * BLOCK_SIZE)));
    int playerChunkY = static_cast<int>(std::floor(playerPos.y / (CHUNK_SIZE * BLOCK_SIZE)));
    int playerChunkZ = static_cast<int>(std::floor(playerPos.z / (CHUNK_SIZE * BLOCK_SIZE)));

    // Calculate unload radius in chunks (slightly larger than load distance for hysteresis)
    int unloadRadiusChunks = static_cast<int>(std::ceil(unloadDistance / (CHUNK_SIZE * BLOCK_SIZE))) + 2;

    std::vector<ChunkCoord> chunksToUnload;

    // Check chunks in a large sphere around the player
    for (int dx = -unloadRadiusChunks; dx <= unloadRadiusChunks; ++dx) {
        for (int dy = -unloadRadiusChunks; dy <= unloadRadiusChunks; ++dy) {
            for (int dz = -unloadRadiusChunks; dz <= unloadRadiusChunks; ++dz) {
                int chunkX = playerChunkX + dx;
                int chunkY = playerChunkY + dy;
                int chunkZ = playerChunkZ + dz;

                // Check if chunk exists
                if (m_world->getChunkAt(chunkX, chunkY, chunkZ) != nullptr) {
                    // Calculate distance
                    glm::vec3 chunkCenter = chunkToWorldPos(chunkX, chunkY, chunkZ);
                    float distance = glm::distance(playerPos, chunkCenter);

                    // If beyond unload distance, mark for removal
                    if (distance > unloadDistance) {
                        chunksToUnload.push_back({chunkX, chunkY, chunkZ});
                    }
                }
            }
        }
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
