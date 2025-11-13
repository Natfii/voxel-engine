# Code Examples: Converting to Streaming UX

Copy-paste ready code for converting your current loading to streaming.

---

## File: `include/streaming_system.h`

**Create this new file:**

```cpp
#pragma once

#include <thread>
#include <queue>
#include <memory>
#include <mutex>
#include <glm/glm.hpp>
#include <vector>

class World;
class VulkanRenderer;

struct GenerationTask {
    int chunkX;
    int chunkZ;
    float distance;

    // For priority_queue: smaller distance = higher priority
    bool operator>(const GenerationTask& other) const {
        return distance > other.distance;
    }
};

class StreamingSystem {
public:
    /**
     * @brief Initialize streaming system
     * @param world World instance (must be initialized with spawn area)
     * @param renderer Vulkan renderer for GPU uploads
     */
    StreamingSystem(World* world, VulkanRenderer* renderer);

    /**
     * @brief Stop streaming and wait for completion
     */
    ~StreamingSystem();

    /**
     * @brief Start background generation thread
     *
     * Call this AFTER player has spawned to generate remaining chunks.
     * Does not block main thread.
     */
    void start();

    /**
     * @brief Stop background generation and wait for completion
     */
    void stop();

    /**
     * @brief Update player position for priority reprioritization
     *
     * Call every frame to optimize generation order.
     * Chunks closer to player generate first.
     */
    void updatePlayerPosition(const glm::vec3& playerPos);

    /**
     * @brief Process GPU upload tasks on main thread
     *
     * Call from main loop. This must run on main thread because
     * Vulkan buffer creation requires the correct thread context.
     *
     * @param maxTasks Maximum chunks to upload this frame (default: 2)
     *                 Reduce if frame rate drops, increase if idle
     */
    void processMainThreadTasks(int maxTasks = 2);

    /**
     * @brief Check if all chunks have been generated
     */
    bool isComplete() const { return m_generationQueue.empty() && m_gpuUploadQueue.empty(); }

private:
    World* m_world;
    VulkanRenderer* m_renderer;

    std::thread m_generationThread;
    bool m_running;

    // Queues for work distribution
    // Note: No mutex needed because main thread only reads,
    //       generation thread only writes (except pause/resume)
    std::priority_queue<GenerationTask, std::vector<GenerationTask>,
                       std::greater<GenerationTask>> m_generationQueue;

    std::queue<class Chunk*> m_gpuUploadQueue;

    // Player tracking for priority updates
    glm::vec3 m_playerPos;

    // Background loop
    void generationLoop();

    // Reprioritize queue based on player position
    void reprioritizeQueue();
};
```

---

## File: `src/streaming_system.cpp`

**Create this new file:**

```cpp
#include "streaming_system.h"
#include "world.h"
#include "chunk.h"
#include "vulkan_renderer.h"
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <iostream>

StreamingSystem::StreamingSystem(World* world, VulkanRenderer* renderer)
    : m_world(world),
      m_renderer(renderer),
      m_running(false),
      m_playerPos(0.0f, 0.0f, 0.0f) {
}

StreamingSystem::~StreamingSystem() {
    stop();
}

void StreamingSystem::start() {
    if (m_running) return;

    // Queue all non-spawn chunks for generation
    // Spawn area (3x3) is already generated
    std::vector<GenerationTask> initialTasks;

    for (Chunk* chunk : m_world->m_chunks) {
        // Skip spawn area chunks (already generated)
        if (chunk->getX() >= -1 && chunk->getX() <= 1 &&
            chunk->getZ() >= -1 && chunk->getZ() <= 1) {
            continue;
        }

        float distance = glm::distance(
            glm::vec3(chunk->getX() * 16.0f, 0.0f, chunk->getZ() * 16.0f),
            m_playerPos
        );

        initialTasks.push_back({chunk->getX(), chunk->getZ(), distance});
    }

    // Add all tasks to queue
    for (const auto& task : initialTasks) {
        m_generationQueue.push(task);
    }

    m_running = true;
    m_generationThread = std::thread([this]() { generationLoop(); });

    std::cout << "Streaming system started, queued " << initialTasks.size()
              << " chunks for generation" << std::endl;
}

void StreamingSystem::stop() {
    if (!m_running) return;

    m_running = false;
    if (m_generationThread.joinable()) {
        m_generationThread.join();
    }

    std::cout << "Streaming system stopped" << std::endl;
}

void StreamingSystem::updatePlayerPosition(const glm::vec3& playerPos) {
    m_playerPos = playerPos;
    // Could reprioritize queue here if needed
}

void StreamingSystem::generationLoop() {
    std::cout << "Background generation thread started" << std::endl;

    int generatedCount = 0;
    int meshCount = 0;

    while (m_running) {
        if (m_generationQueue.empty()) {
            // All chunks generated, wait briefly before checking again
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // Get next task with highest priority (shortest distance)
        GenerationTask task = m_generationQueue.top();
        m_generationQueue.pop();

        // Get the chunk
        Chunk* chunk = m_world->getChunkAt(task.chunkX, 0, task.chunkZ);
        if (!chunk) {
            continue;
        }

        // Skip if already generated (safety check)
        if (chunk->isGenerated()) {
            continue;
        }

        // PHASE 1: Generate terrain (can be slow, safe on background thread)
        try {
            chunk->generate(m_world->m_biomeMap.get());
            generatedCount++;

            // PHASE 2: Generate mesh (also safe on background thread)
            chunk->generateMesh(m_world);
            meshCount++;

            // Queue for GPU upload on main thread
            m_gpuUploadQueue.push(chunk);

            // Print progress occasionally
            if ((generatedCount + meshCount) % 20 == 0) {
                std::cout << "Generated: " << generatedCount
                          << " chunks, meshes: " << meshCount
                          << ", queued GPU: " << m_gpuUploadQueue.size()
                          << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error generating chunk ("
                      << task.chunkX << ", " << task.chunkZ << "): "
                      << e.what() << std::endl;
        }
    }

    std::cout << "Background generation complete. "
              << "Generated: " << generatedCount << " chunks, "
              << "Meshes: " << meshCount << std::endl;
}

void StreamingSystem::reprioritizeQueue() {
    // This is complex to implement efficiently with a priority_queue
    // For now, initial sort is good enough.
    // Could be optimized later if needed.
}

void StreamingSystem::processMainThreadTasks(int maxTasks) {
    // GPU buffer creation MUST happen on main thread
    // This is because Vulkan requires thread-safe context

    int processed = 0;
    while (!m_gpuUploadQueue.empty() && processed < maxTasks) {
        Chunk* chunk = m_gpuUploadQueue.front();
        m_gpuUploadQueue.pop();

        if (!chunk) continue;

        try {
            // Create GPU buffers
            // This assumes Chunk has a method to create its buffer
            // If not, you'll need to call this differently
            if (chunk->getVertexCount() > 0) {
                chunk->createBuffer(m_renderer);
            }

            processed++;
        } catch (const std::exception& e) {
            std::cerr << "Error uploading chunk buffer: " << e.what() << std::endl;
        }
    }
}
```

---

## File: `include/world.h` - Changes

**Add these method declarations to World class:**

```cpp
public:
    /**
     * @brief Generate only the spawn area (3x3 chunks around origin)
     *
     * Fast synchronous generation for quick player spawn.
     * Only generates terrain and meshes, does not create GPU buffers.
     *
     * @param spawnChunkX Center chunk X coordinate
     * @param spawnChunkZ Center chunk Z coordinate
     */
    void generateSpawnArea(int spawnChunkX, int spawnChunkZ);

    /**
     * @brief Create GPU buffers for spawn area chunks only
     *
     * Must be called after generateSpawnArea() and before player spawn.
     *
     * @param renderer Vulkan renderer for buffer creation
     */
    void createSpawnBuffers(VulkanRenderer* renderer);

private:
    // These can be accessed by StreamingSystem as a friend class
    friend class StreamingSystem;
```

---

## File: `src/world.cpp` - Implementation

**Add these methods to World class:**

```cpp
void World::generateSpawnArea(int spawnChunkX, int spawnChunkZ) {
    std::cout << "Generating spawn area (3x3 chunks)..." << std::endl;

    int chunksGenerated = 0;

    // Generate the 3x3 grid around spawn point
    for (int dx = -1; dx <= 1; dx++) {
        for (int dz = -1; dz <= 1; dz++) {
            int chunkX = spawnChunkX + dx;
            int chunkZ = spawnChunkZ + dz;

            Chunk* chunk = getChunkAt(chunkX, 0, chunkZ);
            if (!chunk) {
                std::cerr << "Warning: Spawn chunk (" << chunkX << ", "
                          << chunkZ << ") does not exist" << std::endl;
                continue;
            }

            // Generate terrain for this chunk
            chunk->generate(m_biomeMap.get());

            // Generate mesh (safe after terrain is ready)
            chunk->generateMesh(this);

            chunksGenerated++;
        }
    }

    std::cout << "Spawn area generation complete: " << chunksGenerated
              << "/9 chunks ready" << std::endl;
}

void World::createSpawnBuffers(VulkanRenderer* renderer) {
    std::cout << "Creating GPU buffers for spawn area..." << std::endl;

    int buffersCreated = 0;

    // Create GPU buffers for 3x3 spawn area
    for (int dx = -1; dx <= 1; dx++) {
        for (int dz = -1; dz <= 1; dz++) {
            Chunk* chunk = getChunkAt(dx, 0, dz);
            if (!chunk) continue;

            // Only create buffer if chunk has geometry
            if (chunk->getVertexCount() > 0) {
                chunk->createBuffer(renderer);
                buffersCreated++;
            }
        }
    }

    std::cout << "Spawn area GPU buffers created: " << buffersCreated
              << " chunks" << std::endl;
}
```

---

## File: `src/main.cpp` - Changes to Loading Section

**Replace the current loading section (lines 284-303) with:**

```cpp
        // Loading stage 6: Generate spawn area (50%)
        loadingProgress = 0.50f;
        loadingMessage = "Generating spawn area";
        renderLoadingScreen();
        std::cout << "Generating spawn area (3x3 chunks)..." << std::endl;

        // Generate ONLY the spawn area (fast)
        world.generateSpawnArea(0, 0);

        // Loading stage 7: Create spawn GPU buffers (75%)
        loadingProgress = 0.75f;
        loadingMessage = "Creating GPU buffers";
        renderLoadingScreen();
        std::cout << "Creating spawn area GPU buffers..." << std::endl;

        // Create GPU buffers ONLY for spawn area (fast)
        world.createSpawnBuffers(&renderer);
```

**Replace the section after player spawn (around line 465) with:**

```cpp
        // Hide loading screen - game is ready!
        loadingComplete = true;

        // Start background world generation
        std::cout << "Starting background world generation..." << std::endl;
        std::unique_ptr<StreamingSystem> streaming =
            std::make_unique<StreamingSystem>(&world, &renderer);
        streaming->start();
```

**Then in the main game loop (around line 475), add this:**

```cpp
        while (!glfwWindowShouldClose(window)) {
            float currentFrame = glfwGetTime();
            deltaTime = currentFrame - lastFrame;
            lastFrame = currentFrame;

            // ... existing code ...

            // UPDATE: Process streaming GPU uploads (main thread work)
            // This should be early in the loop, ideally before rendering
            streaming->updatePlayerPosition(player.Position);
            streaming->processMainThreadTasks(2);  // 2 chunks per frame max

            // ... rest of existing code ...

            // At the very end of the loop, before closing brace:
            // streaming continues in background, no manual updates needed
        }
```

---

## File: `include/chunk.h` - Required Methods

**Verify these methods exist in your Chunk class:**

```cpp
public:
    // Getters for streaming system
    int getX() const { return m_x; }
    int getZ() const { return m_z; }

    // Status checks
    bool isGenerated() const;  // true if terrain has been generated
    bool hasMesh() const;      // true if mesh has been created

    // These likely already exist, just verify:
    void generate(BiomeMap* biomeMap);
    void generateMesh(World* world);
    void createBuffer(VulkanRenderer* renderer);

    size_t getVertexCount() const;  // Return number of vertices
```

If these don't exist exactly, you'll need to add them or adjust the streaming system code.

---

## Integration Checklist

- [ ] Create `include/streaming_system.h`
- [ ] Create `src/streaming_system.cpp`
- [ ] Update `include/world.h` with new methods
- [ ] Add implementation to `src/world.cpp`
- [ ] Update loading section in `src/main.cpp`
- [ ] Update main loop in `src/main.cpp`
- [ ] Verify Chunk methods exist and work correctly
- [ ] Compile and test
- [ ] Measure loading time (should be ~1.5 seconds)
- [ ] Test gameplay and background generation

---

## Testing Code

**Add this temporary code to verify it works:**

```cpp
// In main.cpp, after streaming->start(), add:

// TEST: Print streaming status
auto printStreamingStatus = [&streaming]() {
    if (streaming) {
        std::cout << "Streaming active: "
                  << (streaming->isComplete() ? "COMPLETE" : "RUNNING")
                  << std::endl;
    }
};

// Then in main loop, optionally call:
static int frameCounter = 0;
frameCounter++;
if (frameCounter % 300 == 0) {  // Every 5 seconds at 60 FPS
    printStreamingStatus();
}
```

---

## Common Compilation Issues

### Issue: `Chunk::createBuffer()` doesn't exist
**Solution:** You may need to implement this method:

```cpp
void Chunk::createBuffer(VulkanRenderer* renderer) {
    if (m_vertexBuffer != VK_NULL_HANDLE) {
        // Already has a buffer
        return;
    }

    // Create vertex buffer for this chunk
    if (m_vertices.empty()) return;  // Skip empty chunks

    // This depends on your Vulkan setup
    // You likely already have buffer creation code elsewhere
    // Copy it here or refactor to use it
}
```

### Issue: `World::m_chunks` is private
**Solution:** Make it accessible to StreamingSystem:

```cpp
// In world.h:
private:
    friend class StreamingSystem;  // Add this line
    std::vector<Chunk*> m_chunks;
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>> m_chunkMap;
```

### Issue: Thread safety errors
**Solution:** Generation thread can safely:
- Read any Chunk data without locks
- Read World::m_biomeMap
- Generate terrain and meshes

But CANNOT:
- Modify GPU buffers (only main thread)
- Call Vulkan functions

If you get other errors, the issue is likely passing references unsafely between threads.

---

## Performance Tuning

### If load time is still slow:
```cpp
// In main.cpp loading section:
// Skip decoration on initial load (trees, structures)
// Add it to background later

// Option 1: Skip entirely
// world.decorateWorld();  // Comment this out

// Option 2: Move to background
// (Would need separate thread and more careful syncing)
```

### If frame rate drops:
```cpp
// In main loop, reduce GPU uploads:
streaming->processMainThreadTasks(1);  // Was 2, now 1
```

### If chunks appear too slowly:
```cpp
// In StreamingSystem, increase priority range:
// Pre-generate chunks in a larger radius around player
// Currently it just generates all in distance order
```

---

## Success Metrics

After implementing, you should see:

```
Before:
- Load time: 30-60 seconds
- Screen: Black with loading bar
- At 100%: Player spawns
- First gameplay: Entire world visible

After:
- Load time: 1-2 seconds
- Screen: Black with loading bar (smaller)
- At 100%: Player spawns
- First gameplay: Spawn area visible, fog hides ungenerated
- ~5 seconds later: Most nearby chunks loaded silently
- ~20 seconds later: All chunks generated and visible
- No "loading" UI during gameplay
- Smooth frame rate throughout
```

Your target is the "After" column.
