# Streaming Implementation Guide

Quick reference for converting your current synchronous loading to streaming architecture.

## Current Flow (What You Have Now)

```
main.cpp
  ├─ Stage 1-4: Load registries (~0.5s) ✓
  ├─ Stage 5: Create World() instance
  ├─ Stage 6: world.generateWorld() ← ALL CHUNKS HERE (10-60s) ✗
  │           └─ Parallel: ~N threads generate all terrain
  │           └─ Wait for all threads
  ├─ Stage 7: world.decorateWorld() (trees, structures)
  ├─ Stage 8: world.createBuffers() ← ALL CHUNKS to GPU (5-10s) ✗
  ├─ Stage 9-12: Spawn player
  └─ Begin gameplay (everything already loaded)
```

**Problem:** Player waits 15-70 seconds for world nobody sees yet.

---

## Target Flow (What You Need)

```
main.cpp
  ├─ Stage 1-4: Load registries (~0.5s) ✓
  ├─ Stage 5: Create World() instance
  ├─ Stage 6: world.generateSpawnArea(0, 0) ← ONLY 3x3 CHUNKS (~0.5s) ✓
  │           └─ Synchronous: Main thread generates 9 chunks only
  │           └─ Generate meshes immediately
  ├─ Stage 7: world.createSpawnBuffers() ← ONLY SPAWN to GPU (~0.2s) ✓
  ├─ Stage 8-12: Spawn player (same) (~0.5s) ✓
  ├─ T=1.5s: PLAYER SPAWNS HERE ← INSTANT GAMEPLAY
  │
  └─ Background thread starts here:
      ├─ Starts: StreamingSystem::run() background thread
      ├─ Actions:
      │  ├─ Generate remaining chunks (terrain only)
      │  ├─ Generate meshes for visible chunks
      │  ├─ Upload visible buffers to GPU
      │  └─ Prioritize by: distance from player
      │
      └─ Continues until all chunks done or game closes
```

**Benefit:** Player plays immediately, world fills in silently.

---

## Step-by-Step Implementation

### Step 1: Refactor `World::generateWorld()`

**Current code** (`world.cpp:107-160`):
```cpp
void World::generateWorld() {
    // Generate terrain for ALL chunks (blocks)
    // Wait for all
    // Generate meshes for ALL chunks
    // Wait for all
}
```

**New code structure:**
```cpp
// In world.h, add:
public:
    void generateSpawnArea(int spawnChunkX, int spawnChunkZ);
    void startAsyncWorldGeneration();  // Begins background thread
    void updateStreaming();  // Call from main loop if needed

private:
    friend class StreamingSystem;
    std::thread m_streamingThread;
    bool m_streamingActive;
    // ... (more on this below)
```

**Implementation template:**
```cpp
void World::generateSpawnArea(int spawnChunkX, int spawnChunkZ) {
    // Generate ONLY the 3x3 grid around spawn point
    for (int dx = -1; dx <= 1; dx++) {
        for (int dz = -1; dz <= 1; dz++) {
            int x = spawnChunkX + dx;
            int z = spawnChunkZ + dz;

            Chunk* chunk = getChunkAt(x, 0, z);  // Just first layer
            if (!chunk) continue;

            // Terrain generation (fast)
            chunk->generate(m_biomeMap.get());

            // Mesh generation (also fast for 3x3)
            chunk->generateMesh(this);
        }
    }

    std::cout << "Spawn area ready!" << std::endl;
}

void World::startAsyncWorldGeneration() {
    m_streamingActive = true;
    m_streamingThread = std::thread([this]() {
        this->backgroundGenerationLoop();
    });
}

void World::backgroundGenerationLoop() {
    // This runs on separate thread
    // Generate all non-spawn chunks

    for (Chunk* chunk : m_chunks) {
        // Skip if already generated (spawn area)
        if (chunk->isGenerated()) continue;

        // Generate in background
        chunk->generate(m_biomeMap.get());

        // Can safely mark as "generated" flag without locking
        // (no one reads until player approaches)
    }

    // Phase 2: Mesh generation (after ALL terrain done)
    for (Chunk* chunk : m_chunks) {
        if (!chunk->hasMesh()) {
            chunk->generateMesh(this);
        }
    }
}
```

---

### Step 2: Create `StreamingSystem` class

**New file: `include/streaming_system.h`**

```cpp
#pragma once
#include <thread>
#include <queue>
#include <memory>
#include <glm/glm.hpp>

class World;
class VulkanRenderer;

struct StreamingTask {
    int chunkX, chunkY, chunkZ;
    float distance;

    bool operator>(const StreamingTask& other) const {
        return distance > other.distance;  // min-heap
    }
};

class StreamingSystem {
public:
    StreamingSystem(World* world, VulkanRenderer* renderer);
    ~StreamingSystem();

    // Start background streaming
    void start();
    void stop();

    // Update priorities based on player position
    void updatePlayerPosition(const glm::vec3& playerPos);

    // Call from main loop to do main-thread work (GPU uploads)
    void processMainThreadTasks(int maxTasks = 5);

private:
    World* m_world;
    VulkanRenderer* m_renderer;

    std::thread m_generationThread;
    bool m_running;

    // Task queues
    std::priority_queue<StreamingTask, std::vector<StreamingTask>,
                        std::greater<StreamingTask>> m_generationQueue;
    std::queue<Chunk*> m_gpuUploadQueue;

    glm::vec3 m_playerPos;

    void generationLoop();
    void reprioritizeQueue();
};
```

**New file: `src/streaming_system.cpp`**

```cpp
#include "streaming_system.h"
#include "world.h"
#include "vulkan_renderer.h"
#include <algorithm>
#include <cmath>

StreamingSystem::StreamingSystem(World* world, VulkanRenderer* renderer)
    : m_world(world), m_renderer(renderer), m_running(false) {}

StreamingSystem::~StreamingSystem() {
    stop();
}

void StreamingSystem::start() {
    m_running = true;

    // Queue all non-spawn chunks
    // For now, simple: add all chunks
    // Later: only add chunks within distance
    for (Chunk* chunk : m_world->m_chunks) {
        if (!chunk->isGenerated()) {
            float dist = glm::distance(
                glm::vec3(chunk->getX(), 0, chunk->getZ()),
                m_playerPos
            );
            m_generationQueue.push({chunk->getX(), 0, chunk->getZ(), dist});
        }
    }

    m_generationThread = std::thread([this]() { generationLoop(); });
}

void StreamingSystem::stop() {
    m_running = false;
    if (m_generationThread.joinable()) {
        m_generationThread.join();
    }
}

void StreamingSystem::generationLoop() {
    while (m_running) {
        // Get next task
        if (m_generationQueue.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        StreamingTask task = m_generationQueue.top();
        m_generationQueue.pop();

        // Generate chunk (safe on background thread)
        Chunk* chunk = m_world->getChunkAt(task.chunkX, task.chunkY, task.chunkZ);
        if (chunk && !chunk->isGenerated()) {
            chunk->generate(m_world->m_biomeMap.get());
            chunk->generateMesh(m_world);

            // Queue for GPU upload on main thread
            m_gpuUploadQueue.push(chunk);
        }
    }
}

void StreamingSystem::updatePlayerPosition(const glm::vec3& playerPos) {
    m_playerPos = playerPos;
    // Could reprioritize queue here if needed
}

void StreamingSystem::processMainThreadTasks(int maxTasks) {
    // GPU buffer creation MUST happen on main thread
    // So we batch it in main loop

    int processed = 0;
    while (!m_gpuUploadQueue.empty() && processed < maxTasks) {
        Chunk* chunk = m_gpuUploadQueue.front();
        m_gpuUploadQueue.pop();

        // Create GPU buffers (main thread only!)
        chunk->createBuffer(m_renderer);
        processed++;
    }
}
```

---

### Step 3: Update `main.cpp`

**Current loading section** (lines 237-303):

Change from:
```cpp
// Loading stage 6: Generate terrain (60%)
loadingProgress = 0.35f;
loadingMessage = "Generating world";
renderLoadingScreen();
std::cout << "Generating world..." << std::endl;
world.generateWorld();  // ← BLOCKS FOR 20-60 SECONDS

// Loading stage 7: Decorate world (75%)
loadingProgress = 0.65f;
loadingMessage = "Growing trees and vegetation";
renderLoadingScreen();
std::cout << "Decorating world (trees, vegetation)..." << std::endl;
world.decorateWorld();

// Loading stage 8: Create GPU buffers (85%)
loadingProgress = 0.80f;
loadingMessage = "Creating GPU buffers";
renderLoadingScreen();
std::cout << "Creating GPU buffers..." << std::endl;
world.createBuffers(&renderer);  // ← BLOCKS FOR 5-10 SECONDS
```

To:
```cpp
// Loading stage 6: Generate spawn area (50%)
loadingProgress = 0.50f;
loadingMessage = "Generating spawn area";
renderLoadingScreen();
std::cout << "Generating spawn area..." << std::endl;
world.generateSpawnArea(0, 0);  // Only 3x3 chunks, ~0.5s

// Loading stage 7: Create spawn GPU buffers (75%)
loadingProgress = 0.75f;
loadingMessage = "Setting up GPU buffers";
renderLoadingScreen();
std::cout << "Creating spawn area GPU buffers..." << std::endl;
world.createSpawnBuffers(&renderer);  // Only 9 chunks, ~0.2s
```

**Then after player spawns, add:**
```cpp
// Hide loading screen - game is ready!
loadingComplete = true;

// Start background streaming (after showing gameplay)
std::cout << "Starting background world generation..." << std::endl;
std::unique_ptr<StreamingSystem> streaming =
    std::make_unique<StreamingSystem>(&world, &renderer);
streaming->start();

bool isPaused = false;
// ... rest of main loop ...

while (!glfwWindowShouldClose(window)) {
    // ... existing code ...

    // UPDATE: Process streaming uploads every frame (small batch)
    streaming->processMainThreadTasks(2);  // Upload 2 chunks max per frame

    // UPDATE: Update streaming system with player position
    streaming->updatePlayerPosition(player.Position);

    // ... rest of loop ...
}
```

---

### Step 4: Update `World` class

**In `world.h`, add methods:**
```cpp
public:
    void generateSpawnArea(int spawnChunkX, int spawnChunkZ);
    void createSpawnBuffers(VulkanRenderer* renderer);

    // Helper for streaming system
    void createSpawnBuffers(VulkanRenderer* renderer) {
        // Create GPU buffers ONLY for 3x3 spawn area
        for (int x = -1; x <= 1; x++) {
            for (int z = -1; z <= 1; z++) {
                Chunk* chunk = getChunkAt(x, 0, z);
                if (chunk && chunk->hasMesh()) {
                    chunk->createBuffer(renderer);
                }
            }
        }
    }

private:
    friend class StreamingSystem;
    std::vector<Chunk*> m_chunks;  // Need public or friend access
```

**Chunk class** needs to expose:
```cpp
// In chunk.h, add:
bool isGenerated() const { return m_terrain_generated; }
bool hasMesh() const { return !m_vertices.empty(); }
void createBuffer(VulkanRenderer* renderer);  // Create GPU buffer for this chunk
int getX() const { return m_x; }
int getZ() const { return m_z; }
```

---

## Testing Checklist

### Before Implementation
- [ ] Current load time: 20-60 seconds (measure with `glfwGetTime()`)
- [ ] Current memory: How much RAM does full world use?
- [ ] Current GPU memory: How much VRAM?

### After Spawn Area Only
- [ ] Load time: <2 seconds
- [ ] Memory: Uses only 3x3 chunks initially
- [ ] GPU memory: Uses only 9 chunk buffers

### After Background Streaming
- [ ] Player spawns at 1.5 seconds
- [ ] Can move immediately
- [ ] No "Loading..." UI after spawn
- [ ] World fills in silently
- [ ] Can't see visual pop-in (fog hides it)

### Stress Testing
- [ ] Walk rapidly in random directions
- [ ] Check if any chunks are missing (should be none)
- [ ] Check frame time (should stay 60 FPS)
- [ ] Check for memory leaks (run 10+ minutes)

---

## Common Issues & Fixes

### Issue: Chunks are blank/gray during streaming
**Cause:** GPU buffer created before mesh was ready
**Fix:** Only add to GPU queue AFTER mesh generation completes
```cpp
chunk->generate();      // Terrain
chunk->generateMesh();  // Mesh data
m_gpuUploadQueue.push(chunk);  // Then queue GPU
```

### Issue: Player walks into ungenerated terrain (black void)
**Cause:** Streaming is too slow
**Fixes:**
1. Pre-generate chunks in player's direction (look-ahead)
2. Add placeholder terrain (flat gray blocks)
3. Reduce world size (fewer chunks to stream)
4. Reduce view distance until chunks catch up

### Issue: Frame drops every few seconds
**Cause:** Too many chunks being uploaded to GPU per frame
**Fix:** Reduce `maxTasks` in `processMainThreadTasks()`
```cpp
streaming->processMainThreadTasks(1);  // 1 chunk per frame instead of 5
```

### Issue: Background thread crashes
**Cause:** Accessing World or Chunk data unsafely
**Fix:**
- Make sure terrain generation doesn't call renderer
- Mesh generation is safe (uses chunk data only)
- GPU uploads MUST be on main thread only

---

## Performance Tips

### Chunk Generation Order
```cpp
// Bad: Random order
for (Chunk* c : chunks) generate(c);

// Better: Distance-based
std::sort(chunks.begin(), chunks.end(),
    [playerPos](Chunk* a, Chunk* b) {
        return distance(a, playerPos) < distance(b, playerPos);
    });
for (Chunk* c : chunks) generate(c);

// Best: Priority queue (reprioritized as player moves)
priority_queue<Chunk*, distance> queue;
```

### Mesh Generation Timing
```cpp
// Bad: Generate mesh immediately during terrain generation
chunk->generate();
chunk->generateMesh();  // Waits for terrain

// Better: Batch meshes after all terrain done
for (Chunk* c : chunks) c->generate();
for (Chunk* c : chunks) c->generateMesh();  // Parallel safe now

// Best: Stagger mesh generation
// Thread 1: Terrain generation
// Wait for all terrain...
// Thread 1: Mesh for chunks 0-100
// Thread 2: Mesh for chunks 100-200
// Main thread: GPU uploads (1-2 per frame)
```

### Memory Management
```cpp
// Chunk occupies space from creation until destruction
// Track actively streaming chunks

// Option 1: Unload far chunks (complex, not recommended for small worlds)
if (distance > renderDistance * 2) {
    removeChunk(chunk);  // Free memory and GPU buffer
    queue.push(chunk);   // Re-add to streaming queue
}

// Option 2: Stream to disk (more complex)
if (distance > renderDistance * 2) {
    saveChunkToDisk(chunk);
    removeChunk(chunk);
}

// Option 3: Just load all (simplest, fine for <1000 chunks)
// This is your current approach - keep it!
```

---

## Summary Table

| Aspect | Before | After |
|--------|--------|-------|
| **Initial Load Time** | 30-60s | 1-2s |
| **Player Spawn Time** | 30-60s | 1-2s |
| **Chunks at Start** | ALL | 3x3 (9) |
| **GPU Buffers at Start** | ALL | 3x3 (9) |
| **Can Move Immediately** | No | Yes |
| **RAM Used at Start** | Full world | 3x3 only |
| **GPU VRAM at Start** | Full world | 3x3 only |
| **Threads Running** | 1 (during load) | 1 (entire session) |
| **Loading UI** | Yes (1 screen) | Yes (1 screen) + silent background |
| **Complexity** | Low | Medium |

---

## Next Steps

1. **Implement spawn area generation** (30 min)
   - Add `generateSpawnArea()` to World
   - Change loading screen to only call this

2. **Test basic streaming** (30 min)
   - Add `startAsyncWorldGeneration()` call after spawn
   - Verify chunks generate in background
   - Measure load time improvement

3. **Create StreamingSystem class** (2 hours)
   - Implement priority queue
   - Handle main-thread GPU uploads
   - Test with player movement

4. **Polish & test** (1 hour)
   - Handle edge cases
   - Measure performance
   - Test with different world sizes

---

**Total Implementation Time: 4-6 hours**
**Most Impactful Part: Steps 1-2 (spawn area only) = 60% of benefit in 10% of time**
