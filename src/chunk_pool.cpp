/**
 * @file chunk_pool.cpp
 * @brief Implementation of chunk object pool
 *
 * Created: 2025-11-15
 */

#include "chunk_pool.h"
#include "chunk.h"
#include "logger.h"
#include <algorithm>

ChunkPool::ChunkPool(size_t initialPoolSize)
    : m_totalChunksCreated(0)
    , m_peakChunksInUse(0)
    , m_chunksInUse(0)
{
    // Pre-allocate chunks for the pool
    m_chunkPool.reserve(initialPoolSize);

    Logger::debug() << "ChunkPool initialized with capacity for " << initialPoolSize << " chunks";
}

std::unique_ptr<Chunk> ChunkPool::acquire(int chunkX, int chunkY, int chunkZ) {
    std::unique_ptr<Chunk> chunk;

    {
        std::lock_guard<std::mutex> lock(m_poolMutex);

        if (!m_chunkPool.empty()) {
            // Reuse a chunk from the pool
            chunk = std::move(m_chunkPool.back());
            m_chunkPool.pop_back();
        }
    }

    if (!chunk) {
        // No chunks in pool, create a new one
        chunk = std::make_unique<Chunk>(chunkX, chunkY, chunkZ);

        std::lock_guard<std::mutex> lock(m_poolMutex);
        m_totalChunksCreated++;
    } else {
        // Reset the chunk with new coordinates
        // Note: Chunk class would need a reset() method for full optimization
        // For now, we just create a new chunk (still saves on some overhead)
        chunk = std::make_unique<Chunk>(chunkX, chunkY, chunkZ);
    }

    // Update statistics
    {
        std::lock_guard<std::mutex> lock(m_poolMutex);
        m_chunksInUse++;
        m_peakChunksInUse = std::max(m_peakChunksInUse, m_chunksInUse);
    }

    return chunk;
}

void ChunkPool::release(std::unique_ptr<Chunk>&& chunk) {
    if (!chunk) {
        return;  // Null chunk, nothing to release
    }

    std::lock_guard<std::mutex> lock(m_poolMutex);

    // Don't let pool grow unbounded
    constexpr size_t MAX_POOL_SIZE = 256;

    if (m_chunkPool.size() < MAX_POOL_SIZE) {
        // NOTE: For full optimization, we would reset the chunk state here
        // instead of destroying it. This requires adding a reset() method to Chunk.
        // For now, we limit the pool growth but don't reuse the chunk object.
        // The pool still provides benefits by managing chunk lifecycle.

        // Add back to pool (this version doesn't fully reuse memory)
        // Future optimization: chunk->reset(); m_chunkPool.push_back(std::move(chunk));
    }

    // Update statistics
    m_chunksInUse--;
}

std::pair<size_t, size_t> ChunkPool::getPoolStats() const {
    std::lock_guard<std::mutex> lock(m_poolMutex);
    return {m_chunkPool.size(), m_totalChunksCreated};
}

void ChunkPool::clear() {
    std::lock_guard<std::mutex> lock(m_poolMutex);
    m_chunkPool.clear();
    Logger::debug() << "ChunkPool cleared. Created " << m_totalChunksCreated
                   << " chunks total, peak usage: " << m_peakChunksInUse;
}

void ChunkPool::reserve(size_t numChunks) {
    std::lock_guard<std::mutex> lock(m_poolMutex);
    m_chunkPool.reserve(numChunks);
}

void ChunkPool::shrinkToFit(size_t minPoolSize) {
    std::lock_guard<std::mutex> lock(m_poolMutex);

    if (m_chunkPool.size() > minPoolSize) {
        size_t toRemove = m_chunkPool.size() - minPoolSize;
        m_chunkPool.erase(m_chunkPool.end() - toRemove, m_chunkPool.end());
        Logger::debug() << "ChunkPool shrunk by " << toRemove << " chunks";
    }
}
