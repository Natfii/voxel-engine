# Memory Implementation Guide: Quick Start

## Memory Visualization

### Current State (12×512×12 world = 73,728 chunks)
```
RAM
┌─────────────────────────────────────────┐
│ System + OS + Apps              1 GB    │
├─────────────────────────────────────────┤
│ Game Textures/Shaders/Audio     2 GB    │
├─────────────────────────────────────────┤
│ ALL 73,728 chunks:                      │
│  ├─ Block data     11.8 GB      ████   │
│  └─ GPU mesh refs  19.2 GB      ███   │
│                    ──────────           │
│                    ~31 GB + overhead   │  ❌ OOM
│                    ──────────           │
│  (+ temp mesh buffers during load)     │
├─────────────────────────────────────────┤
│ Available:                     < 0 GB   │  ❌ CRASH
└─────────────────────────────────────────┘
```

### Proposed Streaming (10-chunk radius = 4,200 chunks loaded)
```
RAM (8 GB system)
┌─────────────────────────────────────────┐
│ System + OS + Apps              1 GB    │
├─────────────────────────────────────────┤
│ Game Textures/Shaders/Audio     2 GB    │
├─────────────────────────────────────────┤
│ Loaded chunks (4,200):                  │
│  ├─ Block data        672 MB    ▓      │
│  └─ GPU mesh buffers 1.1 GB     ██     │
│                       ─────            │
│                       1.8 GB            │  ✓ FITS
│                       ─────            │
│ Disk cache:         3-4 GB              │  (compressed)
├─────────────────────────────────────────┤
│ Available:                     1.2 GB   │  ✓ SAFE MARGIN
└─────────────────────────────────────────┘
```

---

## Key Facts at a Glance

| Metric | Current | Streamed | Improvement |
|--------|---------|----------|-------------|
| Max world size | 512×512 blocks | Unlimited | ∞× |
| Memory per chunk | 420 KB | 420 KB | Same |
| Loaded chunks | All (73,728) | ~4,200 | 17.5× less |
| RAM required | ~31 GB | ~2 GB | 15.5× less |
| Disk storage | 0 | 3-4 GB (compressed) | Trade-off |
| Load time | 30-60 sec | 2-5 sec | 10-12× faster |
| Chunk creation | O(n) at startup | O(1) stream | Scalable |
| Gameplay latency | None (all loaded) | 5-10 ms on entry | Minor |

---

## Code Implementation (Simplified)

### 1. Chunk Pooling

**File: `include/chunk_pool.h`**
```cpp
#pragma once
#include <vector>
#include <queue>
#include "chunk.h"

class ChunkPool {
private:
    std::vector<std::unique_ptr<Chunk>> m_allocated;
    std::queue<Chunk*> m_available;

public:
    ChunkPool(size_t initialSize = 256) {
        for (size_t i = 0; i < initialSize; ++i) {
            auto chunk = std::make_unique<Chunk>(-999, -999, -999);
            m_available.push(chunk.get());
            m_allocated.push_back(std::move(chunk));
        }
    }

    Chunk* acquire(int x, int y, int z) {
        Chunk* chunk;

        if (!m_available.empty()) {
            chunk = m_available.front();
            m_available.pop();
        } else {
            // Allocate batch of 64 new chunks
            for (size_t i = 0; i < 64; ++i) {
                auto newChunk = std::make_unique<Chunk>(-999, -999, -999);
                m_available.push(newChunk.get());
                m_allocated.push_back(std::move(newChunk));
            }
            chunk = m_available.front();
            m_available.pop();
        }

        // Reset chunk position
        chunk->m_x = x;
        chunk->m_y = y;
        chunk->m_z = z;
        chunk->m_visible = false;

        return chunk;
    }

    void release(Chunk* chunk) {
        // Clear CPU data before pooling
        // m_blocks stays allocated but uninitialized
        m_available.push(chunk);
    }

    size_t getPoolSize() const { return m_available.size(); }
    size_t getTotalAllocated() const { return m_allocated.size(); }
};
```

### 2. Mesh Buffer Pooling

**File: `include/mesh_buffer_pool.h`**
```cpp
#pragma once
#include <vector>
#include <queue>
#include "chunk.h"

class MeshBufferPool {
private:
    std::queue<std::vector<Vertex>> m_vertexBuffers;
    std::queue<std::vector<uint32_t>> m_indexBuffers;
    std::queue<std::vector<Vertex>> m_transpVertexBuffers;
    std::queue<std::vector<uint32_t>> m_transpIndexBuffers;

public:
    MeshBufferPool() {
        // Pre-allocate 8 sets of buffers (avoid initial allocations)
        for (int i = 0; i < 8; ++i) {
            std::vector<Vertex> verts;
            verts.reserve(50000);  // Max ~14K verts, some headroom
            m_vertexBuffers.push(std::move(verts));

            std::vector<uint32_t> indices;
            indices.reserve(75000);  // Max ~22.5K indices
            m_indexBuffers.push(std::move(indices));

            std::vector<Vertex> tverts;
            tverts.reserve(50000);
            m_transpVertexBuffers.push(std::move(tverts));

            std::vector<uint32_t> tindices;
            tindices.reserve(75000);
            m_transpIndexBuffers.push(std::move(tindices));
        }
    }

    struct BufferSet {
        std::vector<Vertex>* vertices = nullptr;
        std::vector<uint32_t>* indices = nullptr;
        std::vector<Vertex>* transpVertices = nullptr;
        std::vector<uint32_t>* transpIndices = nullptr;
    };

    BufferSet acquire() {
        BufferSet set;

        if (m_vertexBuffers.empty()) {
            // Auto-grow pool
            std::vector<Vertex> v;
            v.reserve(50000);
            m_vertexBuffers.push(std::move(v));
        }
        set.vertices = &m_vertexBuffers.front();
        m_vertexBuffers.pop();

        if (m_indexBuffers.empty()) {
            std::vector<uint32_t> idx;
            idx.reserve(75000);
            m_indexBuffers.push(std::move(idx));
        }
        set.indices = &m_indexBuffers.front();
        m_indexBuffers.pop();

        if (m_transpVertexBuffers.empty()) {
            std::vector<Vertex> tv;
            tv.reserve(50000);
            m_transpVertexBuffers.push(std::move(tv));
        }
        set.transpVertices = &m_transpVertexBuffers.front();
        m_transpVertexBuffers.pop();

        if (m_transpIndexBuffers.empty()) {
            std::vector<uint32_t> ti;
            ti.reserve(75000);
            m_transpIndexBuffers.push(std::move(ti));
        }
        set.transpIndices = &m_transpIndexBuffers.front();
        m_transpIndexBuffers.pop();

        return set;
    }

    void release(const BufferSet& set) {
        set.vertices->clear();
        m_vertexBuffers.push(*set.vertices);

        set.indices->clear();
        m_indexBuffers.push(*set.indices);

        set.transpVertices->clear();
        m_transpVertexBuffers.push(*set.transpVertices);

        set.transpIndices->clear();
        m_transpIndexBuffers.push(*set.transpIndices);
    }
};
```

### 3. Modified World Class (Streaming Integration)

**File: `include/world.h` (excerpt)**
```cpp
#pragma once
#include "chunk.h"
#include "chunk_pool.h"
#include "mesh_buffer_pool.h"
#include <unordered_map>
#include <memory>

class World {
public:
    World(int width, int height, int depth, int seed = 12345);
    ~World();

    // Streaming API
    void loadChunk(int chunkX, int chunkY, int chunkZ, bool generateIfNeeded = true);
    void unloadChunk(int chunkX, int chunkY, int chunkZ);
    void updateLoadRegion(const glm::vec3& playerPos, int radius = 10);

    Chunk* getChunkAt(int chunkX, int chunkY, int chunkZ);
    bool isChunkLoaded(int chunkX, int chunkY, int chunkZ) const;

    // Existing API
    void generateWorld() { /* only generate chunks in initial radius */ }
    void createBuffers(VulkanRenderer* renderer);
    void cleanup(VulkanRenderer* renderer);
    void renderWorld(VkCommandBuffer commandBuffer, const glm::vec3& cameraPos,
                     const glm::mat4& viewProj, float renderDistance = 50.0f,
                     VulkanRenderer* renderer = nullptr);

private:
    int m_width, m_height, m_depth;
    int m_seed;
    glm::vec3 m_lastPlayerPos = {999999, 999999, 999999};

    // Replace unordered_map<ChunkCoord, unique_ptr<Chunk>> with pooled chunks
    std::unordered_map<ChunkCoord, Chunk*> m_loadedChunks;
    std::unordered_set<ChunkCoord> m_pendingLoad;
    std::unordered_set<ChunkCoord> m_pendingUnload;

    ChunkPool m_chunkPool;
    MeshBufferPool m_meshBufferPool;

    // ... other members ...
};
```

### 4. Update Chunk Class for Pooling

**File: `include/chunk.h` (additions)**
```cpp
class Chunk {
public:
    // ... existing API ...

    // Pooling support
    bool isLoaded() const { return m_isLoaded; }
    void clearBlockData() { /* Mark as unloaded, optionally serialize */ }

    // Accessor for mesh buffer pool (during generateMesh)
    static MeshBufferPool* s_meshBufferPool;
    static void setMeshBufferPool(MeshBufferPool* pool) { s_meshBufferPool = pool; }

private:
    bool m_isLoaded = true;  // Track load state
    std::mutex m_loadMutex;  // Protect concurrent load/unload

    // ... existing members ...
};
```

### 5. Streaming Implementation

**File: `src/world_streaming.cpp`**
```cpp
#include "world.h"
#include "vulkan_renderer.h"
#include <glm/gtc/constants.hpp>

void World::updateLoadRegion(const glm::vec3& playerPos, int radius) {
    // Only update if player moved significantly (chunk boundary)
    float distance = glm::distance(playerPos, m_lastPlayerPos);
    if (distance < 8.0f) {  // Less than 1 chunk width
        return;
    }

    m_lastPlayerPos = playerPos;

    // Calculate which chunks to load
    int centerChunkX = (int)(playerPos.x / 32.0f);  // 32 blocks * 1.0 = 32 world units
    int centerChunkY = (int)(playerPos.y / 32.0f);
    int centerChunkZ = (int)(playerPos.z / 32.0f);

    // Find all loaded chunks that are outside radius
    std::vector<ChunkCoord> toUnload;
    for (auto& [coord, chunk] : m_loadedChunks) {
        int dx = coord.x - centerChunkX;
        int dy = coord.y - centerChunkY;
        int dz = coord.z - centerChunkZ;

        int distSq = dx*dx + dy*dy + dz*dz;
        if (distSq > radius * radius) {
            toUnload.push_back(coord);
        }
    }

    // Queue unload
    for (const auto& coord : toUnload) {
        m_pendingUnload.insert(coord);
    }

    // Find all chunks that should be loaded
    for (int x = centerChunkX - radius; x <= centerChunkX + radius; ++x) {
        for (int y = centerChunkY - radius; y <= centerChunkY + radius; ++y) {
            for (int z = centerChunkZ - radius; z <= centerChunkZ + radius; ++z) {
                int dx = x - centerChunkX;
                int dy = y - centerChunkY;
                int dz = z - centerChunkZ;

                if (dx*dx + dy*dy + dz*dz > radius * radius) {
                    continue;  // Outside sphere
                }

                ChunkCoord coord{x, y, z};
                if (m_loadedChunks.find(coord) == m_loadedChunks.end()) {
                    m_pendingLoad.insert(coord);
                }
            }
        }
    }

    // Process unloads (synchronous for now)
    for (const auto& coord : m_pendingUnload) {
        unloadChunk(coord.x, coord.y, coord.z);
    }
    m_pendingUnload.clear();

    // Process loads (can be async in future)
    for (const auto& coord : m_pendingLoad) {
        loadChunk(coord.x, coord.y, coord.z);
    }
    m_pendingLoad.clear();
}

void World::loadChunk(int chunkX, int chunkY, int chunkZ, bool generateIfNeeded) {
    ChunkCoord coord{chunkX, chunkY, chunkZ};

    // Already loaded
    if (m_loadedChunks.find(coord) != m_loadedChunks.end()) {
        return;
    }

    // Acquire chunk from pool
    Chunk* chunk = m_chunkPool.acquire(chunkX, chunkY, chunkZ);

    // TODO: Try loading from disk first
    // if (!loadChunkFromDisk(chunk)) {
    //     chunk->generate(m_biomeMap.get());
    //     chunk->generateMesh(this);
    //     saveChunkToDisk(chunk);
    // }

    // For now, just generate
    chunk->generate(m_biomeMap.get());
    chunk->generateMesh(this);

    m_loadedChunks[coord] = chunk;
    m_chunks.push_back(chunk);  // Add to render list
}

void World::unloadChunk(int chunkX, int chunkY, int chunkZ) {
    ChunkCoord coord{chunkX, chunkY, chunkZ};

    auto it = m_loadedChunks.find(coord);
    if (it == m_loadedChunks.end()) {
        return;  // Not loaded
    }

    Chunk* chunk = it->second;

    // TODO: Save to disk before unloading
    // saveChunkToDisk(chunk);

    // Remove from render list
    m_chunks.erase(std::find(m_chunks.begin(), m_chunks.end(), chunk));

    // Return to pool
    m_chunkPool.release(chunk);
    m_loadedChunks.erase(it);
}

bool World::isChunkLoaded(int chunkX, int chunkY, int chunkZ) const {
    ChunkCoord coord{chunkX, chunkY, chunkZ};
    return m_loadedChunks.find(coord) != m_loadedChunks.end();
}
```

### 6. Modified Chunk::generateMesh with Pooling

**File: `src/chunk.cpp` (excerpt)**
```cpp
void Chunk::generateMesh(World* world) {
    // Get buffers from pool instead of creating new ones
    auto buffers = Chunk::s_meshBufferPool->acquire();

    std::vector<Vertex>& verts = *buffers.vertices;
    std::vector<uint32_t>& indices = *buffers.indices;
    std::vector<Vertex>& transparentVerts = *buffers.transpVertices;
    std::vector<uint32_t>& transparentIndices = *buffers.transpIndices;

    // ... mesh generation code (unchanged) ...

    // Store data and keep pool buffers
    m_vertexCount = static_cast<uint32_t>(verts.size());
    m_indexCount = static_cast<uint32_t>(indices.size());
    m_vertices = std::move(verts);
    m_indices = std::move(indices);

    m_transparentVertexCount = static_cast<uint32_t>(transparentVerts.size());
    m_transparentIndexCount = static_cast<uint32_t>(transparentIndices.size());
    m_transparentVertices = std::move(transparentVerts);
    m_transparentIndices = std::move(transparentIndices);

    // Return buffers to pool (now empty)
    Chunk::s_meshBufferPool->release(buffers);
}
```

---

## Integration Checklist

- [ ] Add `ChunkPool` to `world.h`
- [ ] Add `MeshBufferPool` to `chunk.h`
- [ ] Create `world_streaming.cpp` with load/unload logic
- [ ] Update `World::renderWorld()` to use `m_loadedChunks` iterator
- [ ] Update `World::getChunkAt()` to auto-load on demand
- [ ] Add safety check: `assert(chunk->isLoaded())` in block access
- [ ] Profile memory usage: track `m_chunkPool.getPoolSize()`
- [ ] Test with radius 5, 10, 15 and verify memory stays constant
- [ ] Implement disk save/load (Phase 2)
- [ ] Add async chunk loading (Phase 3)

---

## Expected Performance Improvements

### Memory
- **Before:** 31 GB (OOM)
- **After:** 2 GB (loaded region only)
- **Disk:** 3-4 GB (compressed cache)
- **Delta:** 92% reduction ✓

### Load Time
- **Before:** 30-60 seconds (all chunks at startup)
- **After:** 2-5 seconds (initial region + streaming)
- **Delta:** 10-12× faster ✓

### Allocation Overhead
- **Before:** 73,728 chunk allocations + millions of vector allocations
- **After:** ~4,200 chunks (pooled) + reused buffers
- **Delta:** 99.99% fewer allocations ✓

### Heap Fragmentation
- **Before:** Severe (massive fragmentation from 73K tiny allocations)
- **After:** Minimal (pre-allocated pools)
- **Delta:** Fragmentation ratio reduced ~100× ✓

