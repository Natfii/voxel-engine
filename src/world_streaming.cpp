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
        Logger::warn() << "WorldStreaming already running";
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

    // Add new requests to load queue
    if (!newRequests.empty()) {
        std::lock_guard<std::mutex> lock(m_loadQueueMutex);

        for (const auto& request : newRequests) {
            m_loadQueue.push(request);
        }

        // Wake up workers
        m_loadQueueCV.notify_all();
    }

    // TODO: Implement chunk unloading for chunks beyond unloadDistance
    // This would require tracking loaded chunks and removing them from the world
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

                // TODO: Add chunk to world's chunk map
                // This requires modifying World to accept externally-created chunks
                // For now, this is a framework demonstration

                Logger::debug() << "Uploaded chunk (" << chunk->getChunkX() << ", "
                               << chunk->getChunkY() << ", " << chunk->getChunkZ()
                               << ") - Vertices: " << chunk->getVertexCount();

                m_totalChunksLoaded++;
            } catch (const std::exception& e) {
                Logger::error() << "Failed to upload chunk: " << e.what();
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
                Logger::error() << "Worker failed to generate chunk: " << e.what();
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
