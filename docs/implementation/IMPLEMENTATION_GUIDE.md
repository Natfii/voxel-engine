# Progressive Chunk Loading: Implementation Guide

**Target:** Production-ready chunk loading with thread-safe queues
**Effort:** 2-3 days (spread across 3 phases)

---

## Phase 1: Implement Thread-Safe Queues

### File: `chunk_loading_queue.cpp`

```cpp
#include "chunk_loading_queue.h"
#include "logger.h"
#include <chrono>

// ============= ChunkRequestQueue =============

bool ChunkRequestQueue::enqueue(const ChunkRequest& request) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Check size limit (prevent unbounded growth)
    if (m_queue.size() >= MAX_QUEUE_SIZE) {
        Logger::warn() << "ChunkRequestQueue is full, dropping request "
                       << "(" << request.chunkX << ", " << request.chunkY
                       << ", " << request.chunkZ << ")";
        return false;
    }

    m_queue.push(request);
    m_cv.notify_one();  // Wake one sleeping generator
    return true;
}

bool ChunkRequestQueue::dequeue(ChunkRequest& request, bool blocking) {
    std::unique_lock<std::mutex> lock(m_mutex);

    if (blocking) {
        // Wait until queue has items OR shutdown signal
        m_cv.wait(lock, [this] {
            return !m_queue.empty() || m_shutdown;
        });
    }

    // Queue might still be empty if shutdown signal received
    if (m_queue.empty()) {
        return false;
    }

    request = m_queue.front();
    m_queue.pop();
    return true;
}

void ChunkRequestQueue::waitForWork() {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this] {
        return !m_queue.empty() || m_shutdown;
    });
}

void ChunkRequestQueue::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::queue<ChunkRequest> empty;
    std::swap(m_queue, empty);  // Efficient clear
}

size_t ChunkRequestQueue::size() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queue.size();
}

void ChunkRequestQueue::signalShutdown() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_shutdown = true;
    }
    m_cv.notify_all();  // Wake all waiting threads
}

// ============= GeneratedChunkQueue =============

bool GeneratedChunkQueue::enqueue(std::shared_ptr<GeneratedChunkData> data) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_queue.size() >= MAX_QUEUE_SIZE) {
        Logger::warn() << "GeneratedChunkQueue is full, dropping chunk "
                       << "(" << data->chunkX << ", " << data->chunkY
                       << ", " << data->chunkZ << ")";
        return false;  // Drop result if queue full
    }

    m_queue.push(std::move(data));  // Move semantics (efficient transfer)
    return true;
}

bool GeneratedChunkQueue::dequeue(std::shared_ptr<GeneratedChunkData>& data) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_queue.empty()) {
        return false;
    }

    data = std::move(m_queue.front());  // Move semantics
    m_queue.pop();
    return true;
}

size_t GeneratedChunkQueue::size() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queue.size();
}

void GeneratedChunkQueue::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::queue<std::shared_ptr<GeneratedChunkData>> empty;
    std::swap(m_queue, empty);
}

// ============= ChunkLoadingManager =============

ChunkLoadingManager::ChunkLoadingManager() {
    Logger::info() << "ChunkLoadingManager created";
}

ChunkLoadingManager::~ChunkLoadingManager() {
    stopGenerators();
}

void ChunkLoadingManager::initialize(World* world, BiomeMap* biomeMap,
                                     VulkanRenderer* renderer) {
    m_world = world;
    m_biomeMap = biomeMap;
    m_renderer = renderer;

    if (!world || !biomeMap) {
        throw std::runtime_error("ChunkLoadingManager: Invalid pointers");
    }

    Logger::info() << "ChunkLoadingManager initialized";
}

void ChunkLoadingManager::startGenerators(uint32_t numThreads) {
    if (numThreads == 0) {
        // Default: half of available cores
        numThreads = std::max(1u, std::thread::hardware_concurrency() / 2);
    }

    if (!m_generatorThreads.empty()) {
        Logger::warn() << "Generators already running, stopping first";
        stopGenerators();
    }

    Logger::info() << "Starting " << numThreads << " chunk generator threads";

    m_shutdown = false;
    m_activeThreadCount = 0;

    for (uint32_t i = 0; i < numThreads; ++i) {
        m_generatorThreads.emplace_back(
            [this] { generatorThreadMain(); }
        );
    }

    // Wait for all threads to startup
    while (m_activeThreadCount < numThreads) {
        std::this_thread::yield();
    }

    Logger::info() << "All generator threads started";
}

void ChunkLoadingManager::stopGenerators() {
    if (m_generatorThreads.empty()) {
        return;
    }

    Logger::info() << "Stopping " << m_generatorThreads.size()
                   << " generator threads";

    // Signal shutdown and wake all threads
    m_inputQueue.signalShutdown();

    // Wait for threads to finish
    for (auto& thread : m_generatorThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    m_generatorThreads.clear();
    m_shutdown = true;

    Logger::info() << "All generator threads stopped";
}

bool ChunkLoadingManager::requestChunk(int chunkX, int chunkY, int chunkZ,
                                        uint32_t priority) {
    ChunkRequest req;
    req.chunkX = chunkX;
    req.chunkY = chunkY;
    req.chunkZ = chunkZ;
    req.priority = priority;
    req.requestID = m_nextRequestID++;

    return m_inputQueue.enqueue(req);
}

uint32_t ChunkLoadingManager::processCompletedChunks() {
    uint32_t processed = 0;
    std::shared_ptr<GeneratedChunkData> result;

    // Process all completed chunks (non-blocking loop)
    while (m_outputQueue.dequeue(result)) {
        if (!result || !m_world || !m_renderer) {
            continue;  // Skip invalid results
        }

        // Retrieve or create chunk in World
        Chunk* chunk = m_world->getChunkAt(result->chunkX,
                                           result->chunkY,
                                           result->chunkZ);

        if (!chunk) {
            // Create new chunk and add to World
            // CRITICAL: This should be synchronized with World
            auto newChunk = std::make_unique<Chunk>(
                result->chunkX, result->chunkY, result->chunkZ);
            chunk = newChunk.get();
            // TODO: Add World::addChunk() method
            // world->addChunk(std::move(newChunk));
        }

        // Transfer mesh data (move semantics, efficient)
        chunk->m_vertices = std::move(result->vertices);
        chunk->m_indices = std::move(result->indices);
        chunk->m_transparentVertices = std::move(result->transparentVertices);
        chunk->m_transparentIndices = std::move(result->transparentIndices);
        chunk->m_vertexCount = result->vertexCount;
        chunk->m_indexCount = result->indexCount;

        try {
            // Upload to GPU
            chunk->createVertexBuffer(m_renderer);
            m_chunksUploaded++;
            processed++;
        } catch (const std::exception& e) {
            Logger::error() << "Failed to upload chunk buffer: " << e.what();
        }
    }

    return processed;
}

ChunkLoadingManager::LoadStatus ChunkLoadingManager::getStatus() const {
    return {
        m_inputQueue.size(),
        m_outputQueue.size(),
        m_activeThreadCount.load()
    };
}

void ChunkLoadingManager::generatorThreadMain() {
    ++m_activeThreadCount;
    Logger::info() << "Generator thread " << std::this_thread::get_id()
                   << " started";

    while (!m_shutdown) {
        ChunkRequest request;

        // Wait for work or shutdown
        if (!m_inputQueue.dequeue(request, true)) {
            // Shutdown signal received
            break;
        }

        // Generate chunk (releases all locks during work)
        auto result = generateChunk(request);

        if (result) {
            // Submit result to output queue
            if (!m_outputQueue.enqueue(result)) {
                Logger::warn() << "Output queue full, dropped chunk result";
            } else {
                m_chunksGenerated++;
            }
        }
    }

    --m_activeThreadCount;
    Logger::info() << "Generator thread " << std::this_thread::get_id()
                   << " stopped";
}

std::shared_ptr<GeneratedChunkData> ChunkLoadingManager::generateChunk(
    const ChunkRequest& request) {

    auto result = std::make_shared<GeneratedChunkData>();
    result->chunkX = request.chunkX;
    result->chunkY = request.chunkY;
    result->chunkZ = request.chunkZ;
    result->requestID = request.requestID;

    if (!m_world || !m_biomeMap) {
        Logger::error() << "Generator: World or BiomeMap is null";
        return result;  // Return empty result
    }

    try {
        // Create temporary chunk (not stored in World)
        Chunk tempChunk(request.chunkX, request.chunkY, request.chunkZ);

        // Phase 1: Generate terrain (thread-safe, no World access)
        tempChunk.generate(m_biomeMap);

        // Phase 2: Copy neighbor blocks (brief lock)
        // TODO: Implement copyNeighborBlocks()
        // This is the critical synchronization point
        NeighborBlockData neighbors;
        {
            // BRIEF LOCK: ~1ms to copy neighbor data
            std::lock_guard lock(m_world->getChunkMapMutex());

            // Copy each neighbor face
            for (int i = 0; i < 6; ++i) {
                // Get neighbor chunk
                Chunk* neighborChunk = nullptr;
                if (i == 0) neighborChunk = m_world->getChunkAt(
                    request.chunkX-1, request.chunkY, request.chunkZ);
                else if (i == 1) neighborChunk = m_world->getChunkAt(
                    request.chunkX+1, request.chunkY, request.chunkZ);
                // ... etc for all 6 neighbors

                // Copy neighbor's edge blocks
                if (neighborChunk) {
                    // memcpy edge face
                }
            }
        }

        // Phase 3: Generate mesh (no locks needed, uses local neighbor copy)
        // TODO: Create variant of generateMesh() that uses local neighbor data
        // tempChunk.generateMeshLocal(neighbors);
        tempChunk.generateMesh(m_world);  // TEMPORARY: Uses brief locks

        // Phase 4: Copy mesh data to result
        result->vertices = tempChunk.m_vertices;
        result->indices = tempChunk.m_indices;
        result->transparentVertices = tempChunk.m_transparentVertices;
        result->transparentIndices = tempChunk.m_transparentIndices;
        result->vertexCount = tempChunk.getVertexCount();
        result->indexCount = tempChunk.getIndexCount();
        result->transparentVertexCount = tempChunk.getTransparentVertexCount();
        result->transparentIndexCount = tempChunk.getTransparentIndexCount();

    } catch (const std::exception& e) {
        Logger::error() << "Chunk generation failed: " << e.what();
    }

    return result;
}
```

---

## Phase 2: Integrate with World

### Modifications to `world.h`

Add to World class:
```cpp
class World {
private:
    mutable std::mutex m_chunkMapMutex;  // NEW: Protects m_chunkMap

public:
    // NEW: Accessor for generator threads
    std::mutex& getChunkMapMutex() { return m_chunkMapMutex; }

    // NEW: Add chunk from generator thread
    void addGeneratedChunk(int chunkX, int chunkY, int chunkZ,
                          std::shared_ptr<GeneratedChunkData> data) {
        std::lock_guard lock(m_chunkMapMutex);

        // Create or retrieve chunk
        Chunk* chunk = getChunkAt(chunkX, chunkY, chunkZ);
        if (!chunk) {
            auto newChunk = std::make_unique<Chunk>(chunkX, chunkY, chunkZ);
            chunk = newChunk.get();
            m_chunkMap[ChunkCoord{chunkX, chunkY, chunkZ}] = std::move(newChunk);
            m_chunks.push_back(chunk);
        }

        // Transfer mesh data
        chunk->m_vertices = std::move(data->vertices);
        chunk->m_indices = std::move(data->indices);
        // ... etc
    }
};
```

### Modifications to `main.cpp` (Game Loop)

```cpp
void gameLoop() {
    ChunkLoadingManager chunkLoader;
    chunkLoader.initialize(&world, world.getBiomeMap(), renderer.get());
    chunkLoader.startGenerators(1);  // Start with 1 generator thread

    while (isRunning) {
        // ... Input, update, etc ...

        // CRITICAL: Update chunk loading
        {
            glm::vec3 playerPos = player.getPosition();
            int playerChunkX = (int)(playerPos.x / 32);  // 32 = chunk size
            int playerChunkZ = (int)(playerPos.z / 32);

            // Request nearby chunks
            const int LOAD_RADIUS = 3;
            for (int dx = -LOAD_RADIUS; dx <= LOAD_RADIUS; ++dx) {
                for (int dz = -LOAD_RADIUS; dz <= LOAD_RADIUS; ++dz) {
                    int chunkX = playerChunkX + dx;
                    int chunkZ = playerChunkZ + dz;

                    uint32_t distSq = dx*dx + dz*dz;
                    uint32_t priority = std::min(100u, distSq * 5);

                    if (!world.hasChunk(chunkX, 0, chunkZ)) {
                        chunkLoader.requestChunk(chunkX, 0, chunkZ, priority);
                    }
                }
            }

            // Process completed chunks (non-blocking)
            uint32_t processed = chunkLoader.processCompletedChunks();
            if (processed > 0) {
                Logger::debug() << "Processed " << processed << " chunks";
            }
        }

        // Render as usual
        renderer->renderFrame();
    }

    chunkLoader.stopGenerators();  // Clean shutdown
}
```

---

## Phase 3: Optimize & Validate

### Performance Profiling Points

```cpp
// In generatorThreadMain(), measure generation time
auto startTime = std::chrono::high_resolution_clock::now();
auto result = generateChunk(request);
auto duration = std::chrono::high_resolution_clock::now() - startTime;
float ms = duration.count() / 1'000'000.0f;
if (ms > 10.0f) {  // Warn if slow
    Logger::warn() << "Slow chunk generation: " << ms << "ms";
}
```

### Thread Safety Validation

Use ThreadSanitizer:
```bash
cd build
cmake -DCMAKE_CXX_FLAGS="-fsanitize=thread" ..
make
./voxel-engine

# Should report 0 data races
```

### Load Testing

```cpp
void stressTest() {
    int frameCount = 0;
    float totalTime = 0;

    for (frameCount = 0; frameCount < 300; ++frameCount) {
        // Simulate fast player movement
        glm::vec3 playerPos(
            100 + std::sin(frameCount * 0.01f) * 50,
            64,
            100 + std::cos(frameCount * 0.01f) * 50
        );
        player.setPosition(playerPos);

        auto t0 = std::chrono::high_resolution_clock::now();

        // Update chunk loading
        chunkLoader.requestChunk(...);
        chunkLoader.processCompletedChunks();

        auto elapsed = std::chrono::high_resolution_clock::now() - t0;
        totalTime += elapsed.count() / 1'000'000.0f;  // ms
    }

    float avgMs = totalTime / frameCount;
    float fps = 1000.0f / avgMs;

    Logger::info() << "Stress test: " << fps << " FPS "
                   << "(" << avgMs << "ms avg)";
    Logger::info() << "Target: 60 FPS (16.67ms)";

    assert(fps >= 55.0f);  // 55 FPS minimum
}
```

---

## Key Implementation Notes

### 1. Memory Ownership

Use move semantics for efficient data transfer:
```cpp
// Generator thread creates result
auto result = std::make_shared<GeneratedChunkData>();
result->vertices = std::move(tempChunk.m_vertices);  // Move, don't copy

// Queue enqueue uses std::move
m_outputQueue.enqueue(std::move(result));

// Main thread dequeue receives ownership
std::shared_ptr<GeneratedChunkData> data;
m_outputQueue.dequeue(data);
chunk->m_vertices = std::move(data->vertices);  // Move to chunk
```

### 2. Lock Duration

CRITICAL: Keep mutex locked duration minimal:
```cpp
{
    std::lock_guard lock(mutex);
    // ONLY do fast operations here (memcpy, no I/O)
}  // Lock released

// Slow operations happen here (mesh generation, memory allocation)
```

### 3. Error Handling

Generator thread errors must not crash system:
```cpp
try {
    auto result = generateChunk(request);
    if (!m_outputQueue.enqueue(result)) {
        // Queue full - acceptable, result dropped
    }
} catch (const std::exception& e) {
    Logger::error() << "Generator error: " << e.what();
    // Continue processing next request
}
```

### 4. Shutdown Sequence

Clean shutdown is critical:
```cpp
void stopGenerators() {
    // Step 1: Signal shutdown
    m_inputQueue.signalShutdown();

    // Step 2: Wake all waiting threads
    // (signalShutdown calls notify_all)

    // Step 3: Wait for threads to finish
    for (auto& thread : m_generatorThreads) {
        thread.join();  // Blocks until thread exits
    }

    // Step 4: Verify empty
    assert(m_inputQueue.size() == 0);
}
```

---

## Debugging Tips

### ThreadSanitizer Output

```
WARNING: ThreadSanitizer: data race
Write of size 4 at 0x7fff12345678 by thread T2:
    #0 ChunkRequestQueue::enqueue() chunk_loading_queue.cpp:123
    #1 ChunkLoadingManager::requestChunk() chunk_loading_queue.cpp:456

Previous read of size 4 at 0x7fff12345678 by thread T3:
    #0 ChunkRequestQueue::dequeue() chunk_loading_queue.cpp:145
```

**Fix:** Add mutex lock around the data access.

### Deadlock Detection

```cpp
// If program hangs, it's likely deadlock
// Check for:
// 1. Multiple locks acquired in different order
// 2. Lock held during wait() call
// 3. Shared state accessed without lock
```

### Slow Chunk Generation

```
[WARN] Slow chunk generation: 25.5ms
```

**Solutions:**
- Use single generator (less contention)
- Reduce neighbor copy overhead (use bitset instead of full copy)
- Profile with perf/VTune to find bottleneck

---

## Validation Checklist

Before production deployment:

- [ ] ThreadSanitizer: 0 data races
- [ ] Helgrind: 0 lock ordering errors
- [ ] Stress test: 60+ FPS sustained
- [ ] Memory test: No leaks
- [ ] Shutdown: Clean exit, no deadlock
- [ ] Edge cases: Queue full, World null, chunk collision
- [ ] Performance: Chunk gen < 10ms, GPU upload < 5ms

