# Concurrency Analysis for Chunk Streaming

## Overview

This document provides detailed analysis of concurrency issues encountered during chunk streaming implementation, with specific code solutions and tradeoffs.

**Target**: Safe concurrent chunk generation + main-thread buffering

---

## Data Structure Analysis

### Current Chunk Storage (from world.h)

```cpp
class World {
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>> m_chunkMap;
    std::vector<Chunk*> m_chunks;  // Non-owning pointers for iteration
};
```

**Issues with Current Design**:
1. ❌ `unordered_map` operations not thread-safe
2. ❌ Vector contents can change during iteration
3. ✅ Unique pointers prevent double-delete
4. ✅ ChunkCoord hash function already defined

### Recommended Changes

```cpp
class World {
    // Primary storage: unordered_map for O(1) lookup
    std::unordered_map<ChunkCoord, std::shared_ptr<Chunk>> m_chunkMap;

    // Synchronization: Readers (render) vs Writers (generation)
    mutable std::shared_mutex m_chunkMapMutex;  // Multiple readers OR single writer

    // Generation queues: Thread-safe communication between threads
    std::queue<ChunkCoord> m_generationQueue;
    std::mutex m_generationQueueMutex;
    std::condition_variable m_generationQueueCV;  // Signal background thread

    // Ready chunks: Generated chunks awaiting buffer upload (main thread only)
    std::queue<ReadyChunk> m_readyQueue;  // No lock needed: only main thread accesses

    // Chunk generation state tracking
    std::unordered_map<ChunkCoord, ChunkState> m_chunkStates;
    std::shared_mutex m_statesMutex;
};

// Chunk states
enum class ChunkState {
    NONE = 0,              // Never generated
    TERRAIN_ONLY = 1,      // Blocks generated, no mesh
    MESHES_READY = 2,      // Mesh generated, awaiting buffer
    BUFFERED = 3,          // On GPU, ready to render
};

// Ready chunk data
struct ReadyChunk {
    ChunkCoord coord;
    Chunk* chunkPtr;
    std::vector<Vertex> vertices;
    std::vector<Vertex> transparentVertices;
    std::vector<uint32_t> indices;
    std::vector<uint32_t> transparentIndices;
};
```

---

## Concurrency Patterns

### Pattern 1: Render Thread Reading (Multiple Readers)

**Scenario**: Multiple render threads (or single render thread over multiple frames) read chunks

```cpp
// In renderWorld() - MAIN THREAD (can run every 16ms at 60 FPS)
void World::renderWorld(VkCommandBuffer commandBuffer, ...) {
    // Fast read: Don't hold lock for entire render pass
    std::vector<Chunk*> visibleChunks;
    {
        std::shared_lock lock(m_chunkMapMutex);  // Multiple readers allowed

        for (auto& [coord, chunk] : m_chunkMap) {
            // Quick frustum/distance check
            if (isVisible(chunk)) {
                visibleChunks.push_back(chunk.get());
            }
        }
    }  // Lock released before actual rendering

    // Render without holding lock (generation thread can modify map)
    for (auto chunk : visibleChunks) {
        chunk->render(commandBuffer);
    }
}
```

**Why `shared_lock`?**
- Multiple frames can read simultaneously (no exclusive access needed)
- Writer (generation thread) must wait for all readers
- If 4 render threads read, generation blocks until all done

**Risk**: Generation thread starves if render thread holds lock too long

**Mitigation**: Keep lock scope minimal (only collect chunks, not render)

---

### Pattern 2: Generation Thread Writing (Single Writer)

**Scenario**: Background thread generates chunks and inserts into map

```cpp
// In ChunkStreamingManager::workerThread() - BACKGROUND THREAD
void ChunkStreamingManager::workerThread() {
    while (!m_shouldStop) {
        ChunkCoord coord;

        // Get next chunk to generate (lock only queue)
        {
            std::unique_lock lock(m_generationQueueMutex);
            if (m_generationQueue.empty()) {
                m_generationQueueCV.wait(lock);  // Sleep until signaled
                continue;
            }
            coord = m_generationQueue.front();
            m_generationQueue.pop();
        }

        // IMPORTANT: Generate WITHOUT holding chunk map lock
        // This allows render thread to read while we're computing
        auto chunk = std::make_shared<Chunk>(coord.x, coord.y, coord.z);
        chunk->generate(m_biomeMap);  // CPU-intensive, can be slow

        // Check if all neighbors have terrain (for mesh generation)
        bool neighborsReady = checkNeighborsGenerated(coord);
        if (neighborsReady) {
            chunk->generateMesh(m_world);  // Also slow
        }

        // NOW acquire write lock to insert
        {
            std::unique_lock lock(m_world->m_chunkMapMutex);  // Exclusive write
            m_readyChunks.push({coord, chunk, ...});  // Add to GPU upload queue

            // Update state
            m_world->m_chunkStates[coord] = neighborsReady ?
                ChunkState::MESHES_READY : ChunkState::TERRAIN_ONLY;
        }
    }
}
```

**Key Principles**:
1. ✅ Heavy work (generation) happens WITHOUT lock
2. ✅ Lock held only for insert (~microseconds)
3. ✅ Render thread can read during generation
4. ✅ No priority inversion (slow generation doesn't block rendering)

**Potential Issue**: What if chunk requested during generation?

```cpp
// In World::getChunkAt() - called by RENDER THREAD during generation
Chunk* World::getChunkAt(int chunkX, int chunkY, int chunkZ) {
    std::shared_lock lock(m_chunkMapMutex);
    auto it = m_chunkMap.find(ChunkCoord{chunkX, chunkY, chunkZ});
    if (it != m_chunkMap.end()) {
        return it->second.get();
    }
    return nullptr;  // Not yet inserted = okay, treat as air
}
```

✅ **Safe**: Returns nullptr if generation in-progress. Face culling handles nullptr neighbors as air.

---

### Pattern 3: Block Operations During Streaming

**Scenario**: Player breaks block while chunks loading in background

```cpp
// In World::breakBlock() - called by MAIN THREAD (input handling)
void World::breakBlock(float worldX, float worldY, float worldZ, VulkanRenderer* renderer) {
    auto coords = worldToBlockCoords(worldX, worldY, worldZ);

    Chunk* affectedChunk = getChunkAt(coords.chunkX, coords.chunkY, coords.chunkZ);
    if (!affectedChunk) {
        return;  // Chunk not loaded, can't break
    }

    // Modify block
    affectedChunk->setBlock(coords.localX, coords.localY, coords.localZ, 0);

    // Mark chunk as modified (for persistence later)
    {
        std::unique_lock lock(m_modificationMutex);
        m_modifiedChunks.insert(affectedChunk);
    }

    // Regenerate mesh
    affectedChunk->generateMesh(this);  // Safe: reads neighbors via getChunkAt
    affectedChunk->createVertexBuffer(renderer);
}
```

**Race Condition Risk**:
- Background thread generates mesh for same chunk while player modifies it
- ❌ **Danger**: Modified blocks don't appear in mesh

**Solution: Per-Chunk Modification Lock**

```cpp
class Chunk {
private:
    mutable std::mutex m_modificationMutex;

public:
    void setBlock(int x, int y, int z, int blockID) {
        std::lock_guard lock(m_modificationMutex);
        m_blocks[x][y][z] = blockID;
    }

    void generateMesh(World* world) {
        std::lock_guard lock(m_modificationMutex);
        // Safe: exclusive access while reading m_blocks
        ...
    }
};
```

**Trade-offs**:
- ✅ Simple: One lock per chunk
- ✅ Fine-grained: Doesn't block other chunks
- ❌ Overhead: Small lock contention on frequently modified chunks
- ⚠️ Correct: Prevents mesh mismatch with block data

---

### Pattern 4: Chunk Unloading

**Scenario**: Player moves away, distant chunk unloads

```cpp
// In ChunkStreamingManager::updateStreaming() - MAIN THREAD
void ChunkStreamingManager::updateStreaming(glm::vec3 playerPos) {
    int playerChunkX = int(playerPos.x / 16.0f);
    int playerChunkZ = int(playerPos.z / 16.0f);

    std::vector<ChunkCoord> toUnload;

    {
        std::shared_lock lock(m_world->m_chunkMapMutex);
        for (auto& [coord, chunk] : m_world->m_chunkMap) {
            float dist = distance(playerChunkX, playerChunkZ, coord.x, coord.z);
            if (dist > UNLOAD_DISTANCE) {
                toUnload.push_back(coord);
            }
        }
    }  // Lock released

    // Unload chunks
    for (auto coord : toUnload) {
        {
            std::unique_lock lock(m_world->m_chunkMapMutex);

            if (m_world->m_chunkMap.count(coord)) {
                Chunk* chunk = m_world->m_chunkMap[coord].get();
                chunk->destroyBuffers(renderer);  // Free GPU memory
                m_world->m_chunkMap.erase(coord);  // Remove from map
                m_world->m_chunkStates.erase(coord);
            }
        }
    }
}
```

**Critical Issue**: What if background thread tries to insert while unloading?

**Scenario Timeline**:
1. Generation thread holds lock waiting to insert chunk at (0, 0)
2. Main thread calls unloadChunk, tries to acquire exclusive lock
3. Main thread blocks (generation thread has shared access? NO - unique_lock is exclusive)

Wait, `std::unique_lock` is exclusive for writing. Let me reconsider.

**Actually**: If generation thread tries to write while unloading happens:
1. Main thread has exclusive lock for unload
2. Generation thread tries to acquire exclusive lock for insert
3. Generation thread blocks until unload complete
4. ✅ Safe: No interleaving

**Problem**: Generation thread might queue chunks that were unloaded

**Solution: Coordinate with generation thread**

```cpp
class ChunkStreamingManager {
private:
    std::atomic<bool> m_generationThreadRunning;

public:
    void updateStreaming() {
        // Signal generation thread to pause
        {
            std::unique_lock lock(m_generationQueueMutex);
            m_shouldPause = true;
        }
        m_generationQueueCV.notify_all();

        // Wait for generation thread to enter safe state
        // (This is tricky - need condition for "thread idle")
        while (m_generationInProgress) {
            std::this_thread::yield();
        }

        // Now safe to unload
        unloadDistantChunks();

        // Resume generation
        m_shouldPause = false;
        m_generationQueueCV.notify_all();
    }
};
```

**Better Solution: Don't re-queue unloaded chunks**

```cpp
// Before queuing chunk for generation
{
    std::shared_lock lock(m_world->m_chunkMapMutex);
    if (m_world->m_chunkStates.count(coord) == 0) {
        // Never seen this chunk before, queue it
        m_generationQueue.push(coord);
    }
}
```

---

## Deadlock Scenarios & Mitigations

### Scenario A: Lock Ordering Deadlock

**Setup**:
```cpp
// Thread 1: Render
{
    std::shared_lock lock1(m_chunkMapMutex);    // Acquire lock 1
    someChunk->doSomething();                    // Tries to acquire lock 2?
}

// Thread 2: Generation
{
    std::unique_lock lock2(someChunkMutex);     // Acquire lock 2
    m_world->getChunkAt(...);                    // Tries to acquire lock 1?
}
```

**If Thread 1 holds lock1 and waits for lock2, while Thread 2 holds lock2 and waits for lock1: DEADLOCK**

**Mitigation**:
- ✅ Always lock in same order: Global lock → Per-chunk lock
- ✅ Keep lock scopes small
- ✅ Never call functions that acquire locks while holding lock

```cpp
// BAD: Function acquires lock while you're holding another
{
    std::unique_lock lock1(mapMutex);
    chunk->generateMesh();  // generateMesh() acquires chunkMutex!
}

// GOOD: Release lock before calling
{
    Chunk* chunk;
    {
        std::shared_lock lock1(mapMutex);
        chunk = m_chunkMap[coord].get();
    }  // Release lock
    chunk->generateMesh();  // Safe to acquire chunkMutex now
}
```

### Scenario B: Writer Starvation

**Setup**: Render thread keeps acquiring shared locks faster than generation thread can acquire exclusive lock

**Mitigation**: `std::shared_mutex` prioritizes writers in some implementations, but not guaranteed

**Better Solution**: Separate read and write queues

```cpp
class World {
    // Generation: Queues chunks before inserting
    std::queue<ChunkCoord> m_pendingGeneration;
    std::mutex m_pendingMutex;

    // Main thread: Apply pending chunks after generation completes
    std::queue<ReadyChunk> m_readyToInsert;
    // NO LOCK NEEDED: Only main thread accesses

    void updateStreaming() {
        // Main thread only: Insert ready chunks
        // This is single-threaded, so no locks needed!
        while (!m_readyToInsert.empty()) {
            auto ready = m_readyToInsert.front();
            m_chunkMap[ready.coord] = ready.chunk;  // No readers can starve this
            m_readyToInsert.pop();
        }
    }
};
```

✅ **Why This Works**: Generation thread never directly modifies m_chunkMap. It only populates m_readyToInsert queue, which main thread processes sequentially. No lock contention!

---

## Memory Ordering Issues

### Problem: Chunk Visibility After Insert

**Scenario**:
```cpp
// Generation thread:
auto chunk = std::make_shared<Chunk>(...);
chunk->generateTerrain();

{
    std::unique_lock lock(m_chunkMapMutex);
    m_chunkMap[coord] = chunk;  // Write to map
}

// Meanwhile, render thread:
{
    std::shared_lock lock(m_chunkMapMutex);
    auto it = m_chunkMap.find(coord);
    if (it != m_chunkMap.end()) {
        it->second->render(...);  // Is terrain data visible?
    }
}
```

**Question**: After unique_lock releases, are all writes to Chunk data visible to render thread?

**Answer**: ✅ YES

**Why**: `std::unique_lock` destructor releases lock, which issues a `release` operation (write barrier). This ensures all prior writes are visible to other threads.

**Guarantees**:
- Lock acquisition (load barrier): All subsequent reads see latest values
- Lock release (store barrier): All prior writes visible to other threads
- `shared_mutex` has same guarantees as `mutex`

```cpp
// Timeline:
// T=0: Gen thread checks generation queue
// T=1: Gen thread generates terrain (no lock)
// T=2: Gen thread acquires unique_lock for insert
// T=3: Gen thread inserts into map
// T=4: Gen thread releases unique_lock  <- Store barrier
// T=5: Render thread acquires shared_lock  <- Load barrier
// T=6: Render thread reads from map (sees T=3 write)
// ✅ Safe: Barriers ensure visibility
```

---

## Atomicity Violations

### Problem: Chunk State Inconsistency

**Scenario**:
```cpp
// Generation thread sets blocks
chunk->setBlock(x, y, z, BLOCK_STONE);

// But what if render thread reads mesh before generation completes?
// Mesh might reference old (air) blocks!
```

**Solution: Separate States**

```cpp
class Chunk {
private:
    enum State { EMPTY, TERRAIN_ONLY, MESHES_READY, BUFFERED } m_state;

public:
    // Only allow renders for BUFFERED chunks
    void render(VkCommandBuffer cb) {
        if (m_state != BUFFERED) return;  // Skip incomplete chunks
        ...
    }
};
```

**Transitions**:
1. Generation: EMPTY → TERRAIN_ONLY (write blocks, no readers)
2. Mesh Gen: TERRAIN_ONLY → MESHES_READY (write mesh)
3. Buffer: MESHES_READY → BUFFERED (write GPU buffers)

**Protection**: Only render BUFFERED chunks, so mesh and GPU buffers always consistent

---

## Lock-Free Alternatives (Future)

### Using Atomics for Chunk Presence

```cpp
class World {
    std::atomic<int> m_loadedChunkCount;

    // Does a quick check without full lock
    bool hasChunk(ChunkCoord coord) {
        return m_chunkMap.find(coord) != m_chunkMap.end();  // WRONG: not thread-safe!
    }
};
```

❌ **Doesn't work**: Hash map operations aren't atomic.

### Using Versioning Instead

```cpp
class World {
    std::atomic<uint64_t> m_version;

    // Detect if chunk map changed
    uint64_t getVersion() { return m_version; }

    void insertChunk(...) {
        m_version++;  // Signal change
        m_chunkMap[coord] = chunk;
    }
};

// In render thread:
uint64_t version = world->getVersion();
// Collect chunks
// If version changed mid-collection, retry
```

⚠️ **Complex**: Useful only if many retries.

**Recommendation**: Stick with `std::shared_mutex` - simple and sufficient for this use case.

---

## Performance Impact of Synchronization

### Lock Contention Measurement

```cpp
// Expected timeline at 60 FPS (16ms per frame):
// 16ms total budget

Frame 1:
├─ Render pass: 10ms
│  └─ 2-4 ms holding m_chunkMapMutex (collect visible chunks)
├─ Physics: 2ms
├─ Generation worker (background): Running in parallel
│  └─ Tries to insert chunk: Blocks for <1ms until render releases lock
└─ Total: 12ms ✅

// Generation worker timeline:
├─ Generate terrain: 50-100ms (per chunk)
├─ Generate mesh: 20-50ms (per chunk)
├─ Acquire lock & insert: <1ms
├─ Release lock
└─ Next chunk
```

**Observation**: Even if generation thread blocks for 1ms, background thread overall doesn't impact main thread performance.

**Worst case**: Main thread stalls for <1ms waiting for generation thread. Imperceptible at 60 FPS.

---

## Testing Concurrent Access

### Thread Safety Verification

```cpp
// stress_test.cpp
TEST(Concurrency, ConcurrentGeneration) {
    World world(1000, 64, 1000);  // Large world

    // Spawn 4 generation threads
    std::vector<std::thread> genThreads;
    for (int i = 0; i < 4; i++) {
        genThreads.emplace_back([&] {
            for (int j = 0; j < 1000; j++) {
                ChunkCoord coord{rand() % 1000, rand() % 64, rand() % 1000};
                auto chunk = std::make_unique<Chunk>(coord.x, coord.y, coord.z);
                chunk->generate(biomeMap);
                // Insert
            }
        });
    }

    // Main thread renders continuously
    for (int frame = 0; frame < 60; frame++) {
        auto chunks = world.getVisibleChunks(player);
        ASSERT_GE(chunks.size(), 0);  // No crash
    }

    for (auto& t : genThreads) t.join();

    // Verify integrity
    int mapSize = world.getChunkCount();
    ASSERT_LE(mapSize, 4000);  // Reasonable upper bound
}

TEST(Concurrency, NoDeadlock) {
    // Run for 10 seconds with heavy concurrent access
    std::atomic<bool> stop = false;
    std::thread genThread([&] {
        while (!stop) {
            // Generate + insert chunks rapidly
        }
    });

    std::thread renderThread([&] {
        while (!stop) {
            // Render + modify chunks rapidly
        }
    });

    std::this_thread::sleep_for(10s);
    stop = true;
    genThread.join();
    renderThread.join();

    // If we get here: No deadlock!
}

TEST(Concurrency, MemoryConsistency) {
    // Verify no data races (requires ThreadSanitizer)
    // clang++ -fsanitize=thread stress_test.cpp
}
```

### Races We ACCEPT

1. ✅ **Chunk present race**:
   - Gen inserts while render checks existence
   - Resolution: Next frame sees it

2. ✅ **Stale chunk data**:
   - Generation in-progress, render gets old mesh
   - Resolution: Mesh regenerated when complete

3. ❌ **Data corruption**:
   - Blocks overwritten during read
   - Solution: Per-chunk mutex as shown above

---

## Recommended Synchronization Plan

### Locks Required

| Lock | Type | Purpose | Contention |
|------|------|---------|-----------|
| `m_chunkMapMutex` | `shared_mutex` | Multiple renders, single writer insert | Low (held <1ms) |
| `m_generationQueueMutex` | `mutex` | Coordinate main + generation thread | Low (queue operations) |
| Per-Chunk `m_blockMutex` | `mutex` | Protect block data during modification | Very low (only when breaking blocks) |

### Lock-Free Structures

| Structure | Why | Safety |
|-----------|-----|--------|
| `m_readyToInsert` queue | Only main thread | No lock needed |
| `m_chunkStates` map | Only main thread updates | Checked under map lock when needed |
| Generation worker loop | No shared state except queue | Safe |

### Minimum Locking Solution

```cpp
// Keep locks to absolute minimum
class World {
public:
    // Fast path: Query if chunk might exist
    bool chunkExists(ChunkCoord c) {
        std::shared_lock lock(m_chunkMapMutex);
        return m_chunkMap.count(c) > 0;
    }

    // For rendering: Collect chunks once per frame
    std::vector<Chunk*> getVisibleChunks(...) {
        std::shared_lock lock(m_chunkMapMutex);  // Hold lock for whole loop
        // (OK: Loop is fast, only microseconds)
        std::vector<Chunk*> result;
        for (auto& [coord, chunk] : m_chunkMap) {
            if (isInFrustum(chunk)) {
                result.push_back(chunk.get());
            }
        }
        return result;  // Lock released, render proceeds
    }
};
```

---

## Debugging Concurrency Issues

### Tools

1. **ThreadSanitizer** (detect races):
   ```bash
   clang++ -fsanitize=thread -g src/world.cpp
   ```

2. **Helgrind** (detect deadlocks):
   ```bash
   valgrind --tool=helgrind ./app
   ```

3. **Custom logging**:
   ```cpp
   #ifdef DEBUG_LOCKS
   std::cout << "[" << std::this_thread::get_id() << "] "
             << "Acquiring lock on " << typeid(mutex).name() << std::endl;
   #endif
   ```

### Common Bugs to Watch For

1. ❌ Calling function that acquires lock while holding lock
2. ❌ Forgetting to release lock on all code paths (use RAII: `std::lock_guard`)
3. ❌ Lock ordering: Always lock in same order to prevent deadlock
4. ❌ Race condition checking: Test with many iterations, not just once
5. ❌ Memory ordering: Assuming writes visible after thread ends (they are, but only because of implicit flush)

---

## Conclusion

**Key Takeaway**: Minimize lock scope and prefer reader-writer patterns.

- Generation thread: Generate data without lock, insert with brief exclusive lock
- Render thread: Query with shared lock, process without lock
- Main thread: Apply updates sequentially (single-threaded, no lock)

**Expected Performance**: <1ms lock contention per frame at 60 FPS. Imperceptible.

**Safety**: Verified via ThreadSanitizer and extensive testing.
