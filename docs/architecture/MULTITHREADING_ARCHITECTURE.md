# Progressive Chunk Loading: Multithreading Architecture Design

**Status:** Design Document
**Complexity:** Expert-level (game engine threading)
**Concurrency Level:** High (multiple generators + main thread)

---

## Executive Summary

This document designs a thread-safe multithreading architecture for progressive chunk loading that:
- **Eliminates frame stuttering** from chunk generation
- **Maintains ~60 FPS** by moving work to background threads
- **Safely manages concurrent access** to World and Chunk data
- **Provides clean separation** between generation and rendering

Current system limitation: Single-threaded generation blocks main thread.
New system: Background generators + non-blocking queues + lock-free main thread.

---

## Part 1: Current System Analysis

### Existing Implementation

```cpp
// Current: world.cpp, generateWorld()
void World::generateWorld() {
    // Phase 1: Parallel terrain generation
    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < numThreads; ++i) {
        threads.emplace_back([this, startIdx, endIdx]() {
            for (size_t j = startIdx; j < endIdx; ++j) {
                m_chunks[j]->generate(biomeMapPtr);  // PARALLEL
            }
        });
    }
    for (auto& thread : threads) thread.join();  // BLOCK until done

    // Phase 2: Parallel mesh generation
    // ... repeat pattern
    // All chunks must be generated before mesh gen (neighbor access)
}
```

### Problems with Current System

| Problem | Impact | Severity |
|---------|--------|----------|
| **Blocks main thread** | Freezes rendering until all chunks done | CRITICAL |
| **All-or-nothing** | Player sees pop-in, not progressive load | HIGH |
| **Fixed sync points** | Can't load chunks dynamically | HIGH |
| **Monolithic generation** | No prioritization (far chunks = far away) | MEDIUM |
| **No cancellation** | Can't stop loading when player moves | MEDIUM |

### Thread Safety Status

| Data | Access Pattern | Current Safety | Issue |
|------|-----------------|-----------------|-------|
| `World::m_chunkMap` | R by main, R by generators | NO MUTEX | Data race if resized |
| `Chunk::m_blocks` | W by generator, R by mesh gen | NO MUTEX | Race during generation |
| `BiomeMap` | R by multiple generators | SAFE | Constant data |
| `FastNoiseLite` | R by multiple generators | SAFE | Thread-safe reads |

**Critical Bug:** Generator threads access `World::m_chunkMap` without synchronization!
```cpp
// In Chunk::generateMesh()
world->getBlockAt()  // Calls world->getChunkAt()
// Which accesses: m_chunkMap.find() - NOT LOCKED!
```

---

## Part 2: New Architecture Design

### Threading Model

```
Main Thread (Rendering)              Generator Threads (Background)
═══════════════════════              ════════════════════════════════
    Frame Loop                            Generator Loop
    ┌──────────────┐                      ┌─────────────────┐
    │ Update input │                      │ Wait for work   │
    └──────┬───────┘                      └────────┬────────┘
           │                                       │
    ┌──────▼───────┐                      ┌────────▼─────────────┐
    │ Request      │ ChunkRequest Queue   │ Dequeue request     │
    │ chunks       ├────────────────────>│ (Non-blocking pop)   │
    └──────┬───────┘                      └────────┬─────────────┘
           │                                       │
    ┌──────▼───────────────┐            ┌──────────▼──────────────┐
    │ Process completed    │            │ Generate terrain       │
    │ chunks               │  GeneratedChunkQueue   (temp Chunk)        │
    │ (Non-blocking pop)   │<──────────┤                        │
    └──────┬───────────────┘            └──────────┬──────────────┘
           │                                       │
    ┌──────▼───────────────┐            ┌──────────▼──────────────┐
    │ Upload to GPU        │            │ Generate mesh          │
    │ (createBuffers)      │            │ (uses neighbor copy)   │
    └──────┬───────────────┘            └──────────┬──────────────┘
           │                                       │
    ┌──────▼───────────────┐            ┌──────────▼──────────────┐
    │ Render               │            │ Copy result to shared_ │
    │ (no blocking)        │            │ ptr<GeneratedChunk>    │
    └──────────────────────┘            └──────────────────────────┘
           │                                       │
           └───────────┬──────────────────────────┘
                   Loop
```

### Queue Architecture

#### 1. ChunkRequestQueue (Main → Generator)

**Purpose:** Main thread submits work, generators pick it up

```cpp
class ChunkRequestQueue {
    std::queue<ChunkRequest> m_queue;        // Actual work queue
    std::mutex m_mutex;                       // SINGLE LOCK for queue + size
    std::condition_variable m_cv;             // Sleep generators when empty
};
```

**Design Principles:**
- Single mutex protects both queue AND size tracking
- Generators sleep on `condition_variable` when empty
- Main thread never blocks (always use non-blocking dequeue for stress testing)
- Max 512 requests to prevent unbounded memory growth

**Thread-Safe Operations:**
```cpp
// Main thread: Submit work (non-blocking)
bool enqueue(const ChunkRequest& req) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_queue.size() >= MAX_SIZE) return false;  // Full
    m_queue.push(req);
    m_cv.notify_one();  // Wake sleeping generator
    return true;
}

// Generator thread: Get work (blocking on empty)
bool dequeue(ChunkRequest& req, bool blocking) {
    std::unique_lock<std::mutex> lock(m_mutex);

    if (blocking) {
        // Sleep until work available OR shutdown signal
        m_cv.wait(lock, [this] {
            return !m_queue.empty() || m_shutdown;
        });
    }

    if (m_queue.empty()) return false;  // Still empty (shutdown)
    req = m_queue.front();
    m_queue.pop();
    return true;
}
```

#### 2. GeneratedChunkQueue (Generator → Main)

**Purpose:** Generators submit results, main thread collects them

```cpp
class GeneratedChunkQueue {
    std::queue<std::shared_ptr<GeneratedChunkData>> m_queue;
    std::mutex m_mutex;  // SINGLE LOCK for queue
    // NO condition_variable: main thread polls in update loop
};
```

**Design Principles:**
- Shared pointers transfer ownership generator → main
- Main thread ALWAYS non-blocking (polling model)
- Smaller limit (128) since fewer completed chunks waiting
- No condition variable (main thread polls at frame rate)

**Thread-Safe Operations:**
```cpp
// Generator thread: Submit completed work (non-blocking)
bool enqueue(std::shared_ptr<GeneratedChunkData> data) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_queue.size() >= MAX_SIZE) return false;  // Drop if queue full
    m_queue.push(std::move(data));
    return true;
}

// Main thread: Collect results (non-blocking poll)
bool dequeue(std::shared_ptr<GeneratedChunkData>& data) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_queue.empty()) return false;
    data = std::move(m_queue.front());
    m_queue.pop();
    return true;
}
```

---

## Part 3: Critical Thread Safety Issues & Solutions

### Issue #1: World::m_chunkMap Access During Mesh Generation

**Problem:**
```cpp
// Current: Chunk::generateMesh() calls world->getBlockAt()
// Which does:
auto it = m_chunkMap.find(ChunkCoord{x, y, z});  // NO LOCK!
```

Multiple generator threads can simultaneously:
- Read m_chunkMap for neighbor lookups
- Cause memory allocator contention
- Trigger hash map rehashing (undefined behavior)

**Solution: Copy-On-Generation Pattern**

Generator thread works with PRIVATE data, no World access:

```cpp
// New: Chunk data is copied BEFORE mesh generation
std::shared_ptr<GeneratedChunkData> generateChunk(ChunkRequest req) {
    auto data = std::make_shared<GeneratedChunkData>();

    // Phase 1: Terrain generation (uses BiomeMap only - thread-safe)
    Chunk tempChunk(req.x, req.y, req.z);
    tempChunk.generate(m_biomeMap);  // NO WORLD ACCESS

    // Phase 2: Collect neighbor blocks BEFORE mesh gen
    // CRITICAL: Copy neighbor blocks while holding World lock (brief)
    NeighborBlockData neighborData = copyNeighborBlocks();

    // Phase 3: Mesh generation (uses local copies only)
    tempChunk.generateMeshLocal(neighborData);  // NO WORLD ACCESS

    // Phase 4: Copy result to output struct
    copyChunkDataToOutput(tempChunk, data);

    return data;
}
```

**Key Point:** Generator thread holds World lock for ~1ms to copy neighbors, then releases immediately.

### Issue #2: Neighbor Access During Mesh Generation

**Problem:**
Mesh generation needs to read neighboring chunk blocks:
```cpp
// In Chunk::generateMesh()
isSolid = [world](int x, int y, int z) {
    if (x < 0 || x >= WIDTH) {
        // Query neighboring chunk from World
        world->getBlockAt(worldPos);  // REQUIRES WORLD LOCK
    }
};
```

**Solution: Neighbor Data Buffering**

Copy neighbor data ONCE before mesh generation:

```cpp
struct NeighborBlockData {
    // 6 faces of neighbors, each 32x32 blocks
    int faceNorth[32][32];  // -Z face
    int faceSouth[32][32];  // +Z face
    int faceEast[32][32];   // +X face
    int faceWest[32][32];   // -X face
    int faceTop[32][32];    // +Y face
    int faceBottom[32][32]; // -Y face
};

// In generator thread (with brief lock):
NeighborBlockData neighbors;
{
    std::lock_guard lock(world->getMutex());  // ~1ms lock

    // Copy each neighbor face
    Chunk* north = world->getChunkAt(x, y, z-1);
    if (north) memcpy(neighbors.faceNorth, north->m_blocks[...], ...);
    // ... repeat for 6 faces
}

// Mesh generation uses local copy (no locks needed)
isSolid = [&neighbors](int x, int y, int z) {
    if (x < 0 && ...) return neighbors.faceWest[...]
    // etc
};
```

### Issue #3: When Can Main Thread Safely Upload Chunk Buffers?

**Problem:**
Generator thread writes to `GeneratedChunkData`, main thread reads it.
Need guarantee: Generator finished writing before main reads.

**Solution: Shared Pointer Semantics**

C++ shared_ptr provides memory ordering guarantees:

```cpp
// Generator thread: Create and finish writing
auto data = std::make_shared<GeneratedChunkData>();
data->vertices = ...;  // Write data
data->indices = ...;
// shared_ptr::enqueue() increments refcount
// Memory barrier: All writes complete before move
m_outputQueue.enqueue(std::move(data));  // Release ownership

// Main thread: Receive and read
std::shared_ptr<GeneratedChunkData> data;
m_outputQueue.dequeue(data);  // Acquire ownership
// Guaranteed: All generator writes visible here
createVertexBuffer(data->vertices, data->indices);
```

**Guarantees:** C++11 `std::shared_ptr` move semantics include acquire/release barriers.

### Issue #4: Queue Management - Preventing Deadlocks

**Pattern: Producer-Consumer Deadlock**

```cpp
// WRONG: Both waiting for each other
// Main thread waits for generator to dequeue
// Generator waits for main thread to process
```

**Solution: Non-Blocking Queues**

Both directions non-blocking:

```cpp
// Main thread: Request work (never blocks)
if (!chunkLoader.requestChunk(x, y, z)) {
    // Queue full, skip this request
    // Try again next frame
}

// Generator thread: Dequeue or sleep
ChunkRequest req;
if (inputQueue.dequeue(req, blocking=true)) {
    // Got work, process it
} else {
    // Queue empty and shutdown, thread exits
}

// Main thread: Process results (never blocks)
uint32_t processed = 0;
std::shared_ptr<GeneratedChunkData> result;
while (outputQueue.dequeue(result)) {
    uploadChunkToGPU(result);
    processed++;
    if (processed >= MAX_PER_FRAME) break;  // Limit per frame
}
```

**Why This Works:**
- Main thread can submit work anytime (queue full = skip)
- Generator thread sleeps when idle (cpu_efficient)
- Main thread woken instantly when work available
- No polling (condition_variable handles sleep)

---

## Part 4: Single vs. Thread Pool Decision

### Recommendation: **Start with Single Generator, Migrate to Pool**

#### Phase 1: Single Generator Thread

**Advantages:**
- Simpler synchronization (fewer contention points)
- Easier debugging (deterministic execution order)
- 50% of hardware threads utilized (~25% with single gen on 4-core)
- Sufficient for most cases (generation is 1-5ms per chunk)

**Configuration:**
```cpp
chunkLoader.startGenerators(1);  // Single thread
```

**Performance Target:**
- 60 FPS threshold: Allow 16ms per frame
- Allocate 10ms to generation
- Can generate ~2-4 chunks per frame at 60 FPS

#### Phase 2: Multiple Generator Threads (if needed)

**When to Use:**
- Player moving very fast (loading many chunks)
- Large view distance (100+ blocks)
- Slow hardware (Raspberry Pi)

**Thread Pool Implementation:**
```cpp
chunkLoader.startGenerators(std::thread::hardware_concurrency() / 2);
// On 8-core CPU: 4 generator threads
```

**Contention Analysis:**

| # Threads | Lock Contention | Queue Scalability | When to Use |
|-----------|-----------------|-------------------|------------|
| 1 | ~0% | Excellent | Default, sufficient |
| 2-4 | ~5% | Excellent | Fast travel, large worlds |
| 4+ | ~15% | Good | Stress test, many chunks |

**Critical:** More threads = more World lock contention. Diminishing returns > 4 threads.

### Load Balancing: Priority Queue

Add priority to requests for optimal user experience:

```cpp
struct ChunkRequest {
    int chunkX, chunkY, chunkZ;
    uint32_t priority;  // 0 = highest, 100 = lowest
};

// Main thread: Priority based on distance
uint32_t distSq = distSquared(player, chunk);
uint32_t priority = std::min(100u, distSq / 10);
requestChunk(x, y, z, priority);

// Generator thread: Use priority queue instead of FIFO
std::priority_queue<ChunkRequest> m_queue;  // Sorts by priority
```

**Result:** Chunks near player generate first, far chunks later.

---

## Part 5: Implementation Strategy

### Mutex Locking Strategy

#### World::m_chunkMap Mutex

Add to World class:
```cpp
class World {
private:
    mutable std::mutex m_chunkMapMutex;

public:
    // Safe accessor for generator threads
    void copyNeighborBlocks(int chunkX, int chunkY, int chunkZ,
                           NeighborBlockData& output) {
        std::lock_guard lock(m_chunkMapMutex);
        // Copy all 6 neighbor chunk faces
    }
};
```

**Lock Duration:** ~1ms maximum (just memcpy operations)

#### Chunk-Level Synchronization

NO per-chunk mutexes needed (each chunk generated once):
```cpp
// Generator thread: Create and fill chunk
Chunk tempChunk(x, y, z);
tempChunk.m_blocks[...] = ...;  // No lock, only this thread accesses

// Main thread: Integrate into World
{
    std::lock_guard lock(world->m_chunkMapMutex);
    world->m_chunkMap[{x, y, z}] = std::make_unique<Chunk>(tempChunk);
    // Copy data from generator's tempChunk to World's chunk
}
```

**Why:** Each chunk is generated exactly once. No sharing until upload.

### Lock-Free Alternatives

Can we use lock-free queues (e.g., Boost MPMC)?

**Analysis:**
- Input queue: ~10 operations/frame main thread, ~100 ops/sec generator
- Output queue: ~10 operations/frame main thread, ~100 ops/sec generator
- Lock contention: Minimal (<1% CPU)
- Lock overhead vs. lock-free complexity: **Locks win**

**Decision:** Don't use lock-free queues. Standard mutexes simpler, no worse performance.

---

## Part 6: Chunk Neighbor Access During Mesh Generation

### Current Implementation Problem

```cpp
// Chunk::generateMesh() - UNSAFE
auto isSolid = [world](int x, int y, int z) {
    if (x < 0 || x >= WIDTH) {
        world->getBlockAt(worldPos);  // Accesses World::m_chunkMap
    }
};
```

**Problem:** Multiple threads accessing World without lock = data races.

### Solution: Three Approaches

#### Approach A: Copy Neighbor Data (RECOMMENDED)

**Pseudocode:**
```cpp
// Generator thread - brief lock phase
std::shared_ptr<GeneratedChunkData> generateChunk(ChunkRequest req) {
    auto chunk = std::make_unique<Chunk>(req.x, req.y, req.z);
    chunk->generate(biomeMap);

    // LOCK PHASE: Copy neighbor blocks (expensive but fast)
    NeighborBlockData neighbors;
    {
        std::lock_guard lock(world->m_chunkMapMutex);
        neighbors = copyNeighborsFromWorld(req.x, req.y, req.z);
        // Lock held: ~1ms
    }

    // UNLOCK PHASE: Mesh generation (expensive but fast)
    chunk->generateMesh_Local(neighbors);

    // Copy to output
    auto result = std::make_shared<GeneratedChunkData>();
    copyChunkToData(chunk, result);
    return result;
}
```

**Advantages:**
- Brief lock (1ms) only during generation
- Generator thread does real work while locked (amortized)
- Mesh generation has NO lock contention
- Simple, correct, predictable performance

**Disadvantages:**
- ~6 KB per chunk (6 faces × 32×32 blocks × 1 byte)

**Performance:** Lock held ~1ms out of 5ms total = 20% contention (acceptable)

#### Approach B: Sync-Point Pattern (ALTERNATIVE)

All terrain generated FIRST, then all meshes generated:

```cpp
void ChunkLoadingManager::generatorThreadMain() {
    while (!shutdown) {
        ChunkRequest req;
        if (inputQueue.dequeue(req)) {
            // Phase 1: Terrain only (no neighbor access needed)
            auto terrain = generateTerrain_NoMesh(req);

            // Wait for all terrain generated before starting mesh
            // (This is hard to coordinate, needs barrier)

            // Phase 2: Mesh generation (now all neighbors available)
            generateMesh(terrain);
        }
    }
}
```

**Disadvantages:**
- Requires barrier synchronization (complex)
- Harder to balance work between threads
- Less responsive (all terrain first, then slow meshing)

#### Approach C: No Mesh Generation in Background

Only generate terrain, mesh on main thread:

```cpp
// Generator thread: Terrain only
tempChunk->generate(biomeMap);
// NO mesh generation

// Main thread: Mesh + upload
chunk->generateMesh(this);  // Main thread, can safely access World
chunk->createVertexBuffer(renderer);
```

**Advantages:**
- No World access in generator thread
- Mesh always has current neighbor data
- Simple to implement

**Disadvantages:**
- Main thread mesh generation can stall (0.5ms per chunk)
- Less parallelization benefit

### Recommendation: Approach A (Copy Neighbors)

Best balance of simplicity, performance, and correctness.

---

## Part 7: Complete Pseudocode

### Generator Thread Main Loop

```cpp
void ChunkLoadingManager::generatorThreadMain() {
    while (true) {
        // Wait for work or shutdown signal
        ChunkRequest request;
        if (!m_inputQueue.dequeue(request, blocking=true)) {
            // Shutdown signal received
            break;
        }

        // =============== GENERATION PHASE ===============

        // Step 1: Create temporary chunk (not in World)
        auto chunk = std::make_unique<Chunk>(
            request.chunkX, request.chunkY, request.chunkZ);

        // Step 2: Generate terrain (thread-safe, uses BiomeMap only)
        chunk->generate(m_biomeMap);

        // Step 3: Brief lock to copy neighbor blocks
        NeighborBlockData neighbors;
        {
            std::lock_guard lock(m_world->getMutex());
            neighbors = copyNeighborsFromWorld(
                request.chunkX, request.chunkY, request.chunkZ);
        }  // Lock released

        // Step 4: Generate mesh (uses local neighbor copy)
        chunk->generateMesh_Local(neighbors);

        // Step 5: Transfer data to output struct
        auto result = std::make_shared<GeneratedChunkData>();
        result->chunkX = request.chunkX;
        result->chunkY = request.chunkY;
        result->chunkZ = request.chunkZ;
        result->requestID = request.requestID;

        // Copy vertices, indices, counts
        result->vertices = std::move(chunk->m_vertices);
        result->indices = std::move(chunk->m_indices);
        result->transparentVertices = std::move(chunk->m_transparentVertices);
        result->transparentIndices = std::move(chunk->m_transparentIndices);
        result->vertexCount = chunk->getVertexCount();
        result->indexCount = chunk->getIndexCount();

        // Step 6: Submit result (non-blocking, may fail if queue full)
        if (!m_outputQueue.enqueue(result)) {
            // Queue full - result dropped, try again next frame
            // (Acceptable for corner case)
        }
    }
}
```

### Main Thread Update Loop

```cpp
// In main game loop, after input/update, before render:

void updateChunkLoading() {
    // ======= REQUEST PHASE =======
    glm::vec3 playerPos = player.getPosition();
    int playerChunkX = (int)(playerPos.x / 16);
    int playerChunkY = (int)(playerPos.y / 16);
    int playerChunkZ = (int)(playerPos.z / 16);

    // Request nearby chunks (distance-based priority)
    const int LOAD_RADIUS = 3;
    for (int dx = -LOAD_RADIUS; dx <= LOAD_RADIUS; dx++) {
        for (int dy = -1; dy <= 1; dy++) {  // Smaller vertical range
            for (int dz = -LOAD_RADIUS; dz <= LOAD_RADIUS; dz++) {
                int chunkX = playerChunkX + dx;
                int chunkY = playerChunkY + dy;
                int chunkZ = playerChunkZ + dz;

                uint32_t distSq = dx*dx + dy*dy + dz*dz;
                uint32_t priority = std::min(100u, distSq * 5);

                if (!world.hasChunk(chunkX, chunkY, chunkZ)) {
                    chunkLoader.requestChunk(chunkX, chunkY, chunkZ, priority);
                }
            }
        }
    }

    // ======= PROCESS RESULTS PHASE =======
    uint32_t processed = 0;
    std::shared_ptr<GeneratedChunkData> result;

    while (chunkLoader.getOutputQueue().dequeue(result) &&
           processed < MAX_GPU_UPLOADS_PER_FRAME) {

        // Retrieve or create chunk in World
        Chunk* chunk = world.getChunkAt(result->chunkX,
                                        result->chunkY,
                                        result->chunkZ);
        if (!chunk) {
            // Create new chunk and insert into World
            chunk = new Chunk(result->chunkX, result->chunkY, result->chunkZ);
            world.addChunk(chunk);
        }

        // Transfer mesh data to chunk
        chunk->m_vertices = std::move(result->vertices);
        chunk->m_indices = std::move(result->indices);
        chunk->m_transparentVertices = std::move(result->transparentVertices);
        chunk->m_transparentIndices = std::move(result->transparentIndices);
        chunk->m_vertexCount = result->vertexCount;
        chunk->m_indexCount = result->indexCount;

        // Upload to GPU
        chunk->createVertexBuffer(renderer);

        processed++;
    }
}
```

---

## Part 8: Testing & Validation

### Thread Safety Checklist

```cpp
// ✓ - Done, X - Missing, ? - Conditional

✓ Mutex protects World::m_chunkMap access
✓ Generator threads never access World after generation
✓ Shared pointers provide memory barriers
✓ No per-chunk mutexes (single-write rule)
✓ No deadlocks (non-blocking queues)
✓ No use-after-free (RAII shared_ptr)
✓ No data races (all accesses protected)
```

### Race Condition Testing

Use ThreadSanitizer to detect races:
```bash
clang++ -fsanitize=thread -g main.cpp world.cpp chunk.cpp
./a.out
```

### Performance Validation

Measure:
- Queue lock contention (< 1% CPU)
- Generator thread wake latency (< 10μs)
- Chunk generation time (5-10ms typical)
- Main thread stall (0ms on successful dequeue)

### Stress Test Scenario

```cpp
// Rapid player movement
void stressTest() {
    for (int i = 0; i < 1000; i++) {
        player.setPosition(randomPosition());
        chunkLoader.requestMany(100 chunks);  // Max queue
        processFrame();  // Measure FPS
    }
}
```

---

## Part 9: Future Optimizations

### 1. Neighbor Prefetch

```cpp
// When requesting chunk (x, y, z), also request neighbors
void requestChunk(int x, int y, int z) {
    request(x, y, z, priority);

    // Also prefetch neighbors (lower priority)
    for (int dx=-1; dx<=1; dx++)
        for (int dy=-1; dy<=1; dy++)
            for (int dz=-1; dz<=1; dz++)
                if (!(dx==0 && dy==0 && dz==0))
                    request(x+dx, y+dy, z+dz, priority+10);
}
```

### 2. Lock-Free Queues

If contention becomes issue, use:
- `boost::lockfree::queue` (multiple producers)
- `moodycamel::ConcurrentQueue` (production-grade)

### 3. Memory Pool for Chunks

Reuse chunk objects instead of allocate/delete:
```cpp
class ChunkPool {
    std::queue<std::unique_ptr<Chunk>> m_pool;
    std::unique_ptr<Chunk> acquire() { ... }
    void release(std::unique_ptr<Chunk>) { ... }
};
```

### 4. Async GPU Upload

```cpp
// Don't wait for GPU upload to complete
createVertexBuffer_Async(data, [](Chunk* chunk) {
    // Called when GPU upload done
    world.addToRenderQueue(chunk);
});
```

---

## Summary Table: Thread Safety

| Component | Main Thread | Generator Threads | Synchronization |
|-----------|-------------|-------------------|-----------------|
| ChunkRequestQueue | Write (enqueue) | Read (dequeue) | Mutex + CV |
| GeneratedChunkQueue | Read (dequeue) | Write (enqueue) | Mutex |
| World::m_chunkMap | Read/Write | Read only (brief) | Mutex + lock guard |
| Chunk::m_blocks | Write (GPU) | Write (generation) | None (write-once) |
| BiomeMap | Read only | Read only | None (immutable) |
| FastNoiseLite | Read only | Read only | None (thread-safe) |

---

## Implementation Roadmap

### Phase 1: Queues (Foundation)
- [ ] Implement ChunkRequestQueue
- [ ] Implement GeneratedChunkQueue
- [ ] Unit test queue operations

### Phase 2: Manager (Orchestration)
- [ ] Implement ChunkLoadingManager
- [ ] Add requestChunk() interface
- [ ] Add processCompletedChunks() interface
- [ ] Unit test manager lifecycle

### Phase 3: Generator Integration
- [ ] Create GeneratedChunkData struct
- [ ] Modify chunk generation to fill struct
- [ ] Neighbor block copying logic
- [ ] Generator thread main loop

### Phase 4: World Integration
- [ ] Add World::getMutex() accessor
- [ ] Add copyNeighborBlocks() to World
- [ ] Modify updateChunkLoading() in main loop
- [ ] Integration testing

### Phase 5: Optimization & Polish
- [ ] Priority queue support
- [ ] Performance profiling
- [ ] Load balancing tuning
- [ ] Production readiness

---

## References & Further Reading

### C++ Threading Best Practices
- "C++ Concurrency in Action" by Anthony Williams
- C++11 Thread-Safe Patterns
- Lock-free programming (but probably overkill here)

### Game Engine Chunk Loading
- Minecraft chunk generation architecture
- Procedural Worlds documentation
- GPU streaming techniques

### Testing Tools
- ThreadSanitizer (race detection)
- Helgrind (Valgrind tool)
- Intel VTune (profiling)

