/**
 * @file mesh_buffer_pool.cpp
 * @brief Implementation of mesh buffer pooling for performance optimization
 *
 * Created: 2025-11-14
 */

#include "mesh_buffer_pool.h"
#include "chunk.h"  // For Vertex struct

MeshBufferPool::MeshBufferPool(size_t initialPoolSize) {
    // Pre-allocate buffers to reduce initial allocation overhead
    reserve(initialPoolSize);
}

std::vector<Vertex> MeshBufferPool::acquireVertexBuffer() {
    if (!m_vertexBufferPool.empty()) {
        // Reuse a buffer from the pool
        std::vector<Vertex> buffer = std::move(m_vertexBufferPool.back());
        m_vertexBufferPool.pop_back();
        buffer.clear();  // Clear contents but keep capacity
        return buffer;
    } else {
        // Create a new buffer
        m_totalVertexBuffersCreated++;
        std::vector<Vertex> buffer;
        buffer.reserve(1024);  // Pre-allocate for typical chunk mesh (~500-2000 vertices)
        return buffer;
    }
}

std::vector<uint32_t> MeshBufferPool::acquireIndexBuffer() {
    if (!m_indexBufferPool.empty()) {
        // Reuse a buffer from the pool
        std::vector<uint32_t> buffer = std::move(m_indexBufferPool.back());
        m_indexBufferPool.pop_back();
        buffer.clear();  // Clear contents but keep capacity
        return buffer;
    } else {
        // Create a new buffer
        m_totalIndexBuffersCreated++;
        std::vector<uint32_t> buffer;
        buffer.reserve(2048);  // Pre-allocate for typical chunk mesh (~1000-4000 indices)
        return buffer;
    }
}

void MeshBufferPool::releaseVertexBuffer(std::vector<Vertex>&& buffer) {
    // Clear the buffer but keep its capacity for reuse
    buffer.clear();

    // Only keep buffers with reasonable capacity to avoid memory waste
    // Buffers larger than 100KB might be outliers (complex chunks)
    const size_t MAX_BUFFER_CAPACITY = 100 * 1024 / sizeof(Vertex);  // ~100KB

    if (buffer.capacity() < MAX_BUFFER_CAPACITY) {
        m_vertexBufferPool.push_back(std::move(buffer));

        // Update peak usage stats
        if (m_vertexBufferPool.size() > m_peakVertexBuffersInUse) {
            m_peakVertexBuffersInUse = m_vertexBufferPool.size();
        }
    }
    // If buffer is too large, let it be destroyed (don't add to pool)
}

void MeshBufferPool::releaseIndexBuffer(std::vector<uint32_t>&& buffer) {
    // Clear the buffer but keep its capacity for reuse
    buffer.clear();

    // Only keep buffers with reasonable capacity to avoid memory waste
    const size_t MAX_BUFFER_CAPACITY = 100 * 1024 / sizeof(uint32_t);  // ~100KB

    if (buffer.capacity() < MAX_BUFFER_CAPACITY) {
        m_indexBufferPool.push_back(std::move(buffer));

        // Update peak usage stats
        if (m_indexBufferPool.size() > m_peakIndexBuffersInUse) {
            m_peakIndexBuffersInUse = m_indexBufferPool.size();
        }
    }
    // If buffer is too large, let it be destroyed (don't add to pool)
}

std::pair<size_t, size_t> MeshBufferPool::getPoolStats() const {
    return {m_vertexBufferPool.size(), m_indexBufferPool.size()};
}

void MeshBufferPool::clear() {
    m_vertexBufferPool.clear();
    m_indexBufferPool.clear();
}

void MeshBufferPool::reserve(size_t numBuffers) {
    m_vertexBufferPool.reserve(numBuffers);
    m_indexBufferPool.reserve(numBuffers);

    // Pre-allocate some buffers
    for (size_t i = 0; i < numBuffers; i++) {
        std::vector<Vertex> vertexBuffer;
        vertexBuffer.reserve(1024);
        m_vertexBufferPool.push_back(std::move(vertexBuffer));

        std::vector<uint32_t> indexBuffer;
        indexBuffer.reserve(2048);
        m_indexBufferPool.push_back(std::move(indexBuffer));

        m_totalVertexBuffersCreated++;
        m_totalIndexBuffersCreated++;
    }
}
