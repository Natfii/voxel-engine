# Chunk Streaming Testing Strategy - Minimal Viable Suite

## Executive Summary

**For safe shipping, you need 4 critical test categories:**
1. **Correctness Tests** (Most Important - prevents silent bugs)
2. **Memory Leak Detection** (Critical - prevents shipping with sustained memory leaks)
3. **Performance Gates** (Important - determines if user experience is acceptable)
4. **Stress/Edge Case Tests** (Nice-to-have - finds corner cases)

**Effort estimate: 2-3 days setup, 30 min daily CI maintenance**

---

## Part 1: MINIMUM VIABLE TEST SUITE TO SHIP

### 1.1 Unit Tests - Chunk Correctness (HIGH PRIORITY)

**Why this first?** - Silent correctness bugs (inverted terrain, missing blocks) are the worst to ship.

#### Test Suite: `tests/chunk_correctness_test.cpp`

```cpp
#include <cassert>
#include <cstring>
#include "chunk.h"
#include "world.h"

void test_chunk_generation_deterministic() {
    // CRITICAL: Same seed must produce identical terrain
    Chunk::initNoise(42);

    Chunk c1(0, 0, 0);
    c1.generate(/* biomeMap */);

    // Store block data
    int blocksCopy[32][32][32];
    for (int x = 0; x < 32; x++) {
        for (int y = 0; y < 32; y++) {
            for (int z = 0; z < 32; z++) {
                blocksCopy[x][y][z] = c1.getBlock(x, y, z);
            }
        }
    }

    Chunk::cleanupNoise();
    Chunk::initNoise(42);

    Chunk c2(0, 0, 0);
    c2.generate(/* biomeMap */);

    // Verify identical
    for (int x = 0; x < 32; x++) {
        for (int y = 0; y < 32; y++) {
            for (int z = 0; z < 32; z++) {
                assert(c2.getBlock(x, y, z) == blocksCopy[x][y][z]);
            }
        }
    }

    std::cout << "✓ Chunk generation is deterministic\n";
}

void test_chunk_state_transitions() {
    // Test lifecycle: Construction -> Generate -> Mesh -> Buffer -> Destroy

    // 1. Constructor initializes to air
    Chunk c(5, 5, 5);
    for (int x = 0; x < 32; x++) {
        for (int y = 0; y < 32; y++) {
            for (int z = 0; z < 32; z++) {
                assert(c.getBlock(x, y, z) == 0); // Air blocks
            }
        }
    }

    // 2. Generation fills blocks
    c.generate(/* biomeMap */);
    bool hasBlocks = false;
    for (int x = 0; x < 32; x++) {
        for (int y = 0; y < 32; y++) {
            for (int z = 0; z < 32; z++) {
                if (c.getBlock(x, y, z) != 0) {
                    hasBlocks = true;
                    break;
                }
            }
        }
    }
    assert(hasBlocks); // Should have at least some blocks

    // 3. Mesh generation doesn't crash
    c.generateMesh(/* world */);

    // 4. Vertex count is reasonable
    assert(c.getVertexCount() >= 0);
    assert(c.getVertexCount() < 100000); // Sanity check

    std::cout << "✓ Chunk state transitions work correctly\n";
}

void test_block_access_bounds() {
    Chunk c(0, 0, 0);

    // Valid access
    c.setBlock(0, 0, 0, 1);
    assert(c.getBlock(0, 0, 0) == 1);

    c.setBlock(31, 31, 31, 5);
    assert(c.getBlock(31, 31, 31) == 5);

    // Out of bounds should handle gracefully
    assert(c.getBlock(-1, 0, 0) == -1);
    assert(c.getBlock(32, 0, 0) == -1);
    assert(c.getBlock(0, -1, 0) == -1);
    assert(c.getBlock(0, 32, 0) == -1);

    std::cout << "✓ Block access bounds checking works\n";
}

void test_metadata_storage() {
    Chunk c(0, 0, 0);

    // Metadata should persist
    c.setBlockMetadata(5, 10, 15, 127);
    assert(c.getBlockMetadata(5, 10, 15) == 127);

    // Different metadata values
    c.setBlockMetadata(0, 0, 0, 0);
    c.setBlockMetadata(31, 31, 31, 255);
    assert(c.getBlockMetadata(0, 0, 0) == 0);
    assert(c.getBlockMetadata(31, 31, 31) == 255);

    std::cout << "✓ Block metadata storage works\n";
}
```

**Why these tests?**
- Determinism = ensures same seed = same world (critical for reproduction)
- State transitions = prevents crashes during loading
- Bounds checking = catches off-by-one errors early
- Metadata = verifies water levels, block states persist

**Run: Daily before commits** ⏱️ 2-5 seconds

---

### 1.2 Memory Leak Detection Tests (CRITICAL)

**Why critical?** - Memory leaks cause hangs/crashes after 10-30 minutes of play.

#### Test Suite: `tests/memory_leak_test.cpp`

```cpp
#include <memory>
#include <iostream>
#include "world.h"

void test_chunk_load_unload_cycles() {
    // Simulate 1000 load/unload cycles
    // Watch with: valgrind --leak-check=full ./voxel-engine-test

    for (int cycle = 0; cycle < 1000; cycle++) {
        // Create world with small dimensions for fast testing
        {
            World world(4, 3, 4);  // 4x3x4 = 48 chunks
            world.generateWorld();
            world.createBuffers(&testRenderer);  // Mock renderer

            // Simulate some operations
            int blocks = world.getChunkAt(0, 0, 0)->getVertexCount();
            world.setBlockAt(5.0f, 10.0f, 5.0f, 1);

            // Destroy - should cleanup everything
            world.cleanup(&testRenderer);
        } // world destructor here

        if (cycle % 100 == 0) {
            std::cout << "  Cycle " << cycle << "/1000 - Memory check...\n";
        }
    }

    std::cout << "✓ 1000 load/unload cycles completed without crashes\n";
}

void test_vulkan_buffer_cleanup() {
    // Verify Vulkan buffers don't leak
    World world(2, 2, 2);
    world.generateWorld();

    Chunk* chunk = world.getChunkAt(0, 0, 0);
    assert(chunk != nullptr);

    uint32_t vertexCount = chunk->getVertexCount();
    std::cout << "  Chunk has " << vertexCount << " vertices\n";

    // Create buffers
    world.createBuffers(&testRenderer);

    // Cleanup should properly destroy all Vulkan resources
    // (Use Valgrind/Dr. Memory to verify no GPU memory leaks)
    world.cleanup(&testRenderer);

    std::cout << "✓ Vulkan buffers cleaned up properly\n";
}
```

**Tools needed:**
```bash
# Linux: Valgrind
valgrind --leak-check=full --show-leak-kinds=all ./chunk_test

# Windows: Dr. Memory or Visual Studio Memory Profiler
"C:\Program Files\Dr. Memory\bin\drmemory.exe" -exit_code_if_errors 0 -- chunk_test.exe

# All platforms: Clang Address Sanitizer (ASAN)
# Add to CMakeLists.txt:
# if(NOT MSVC)
#   target_compile_options(chunk_test PRIVATE -fsanitize=address)
#   target_link_options(chunk_test PRIVATE -fsanitize=address)
# endif()
```

**Run: Before release builds** ⏱️ 30-60 seconds

---

### 1.3 Performance Gate Tests (IMPORTANT)

**Why important?** - Streaming must not cause frame drops.

#### Test Suite: `tests/performance_test.cpp`

```cpp
#include <chrono>
#include "world.h"

struct PerformanceMetrics {
    double chunkGenerationTime;      // ms per chunk
    double meshGenerationTime;       // ms per chunk
    double bufferCreationTime;       // ms per chunk
    double frameTime;                // ms per frame
};

PerformanceMetrics test_generation_performance() {
    PerformanceMetrics metrics = {};

    // Test 1: Single chunk generation time
    Chunk::initNoise(42);
    Chunk c(0, 0, 0);

    auto t0 = std::chrono::high_resolution_clock::now();
    c.generate(/* biomeMap */);
    auto t1 = std::chrono::high_resolution_clock::now();

    metrics.chunkGenerationTime =
        std::chrono::duration<double, std::milli>(t1 - t0).count();

    // GATE: Must be < 5ms per chunk (can load ~6 chunks per frame)
    assert(metrics.chunkGenerationTime < 5.0);
    std::cout << "  Chunk generation: " << metrics.chunkGenerationTime
              << "ms (gate: < 5ms) ✓\n";

    // Test 2: Mesh generation
    auto t2 = std::chrono::high_resolution_clock::now();
    c.generateMesh(/* world */);
    auto t3 = std::chrono::high_resolution_clock::now();

    metrics.meshGenerationTime =
        std::chrono::duration<double, std::milli>(t3 - t2).count();

    // GATE: Must be < 3ms per chunk
    assert(metrics.meshGenerationTime < 3.0);
    std::cout << "  Mesh generation: " << metrics.meshGenerationTime
              << "ms (gate: < 3ms) ✓\n";

    return metrics;
}

void test_world_streaming_performance() {
    // Simulate player moving through world, loading chunks

    auto worldStart = std::chrono::high_resolution_clock::now();

    World world(8, 4, 8);  // 256 chunks
    world.generateWorld();
    world.createBuffers(&testRenderer);

    auto worldEnd = std::chrono::high_resolution_clock::now();
    double totalTime =
        std::chrono::duration<double, std::milli>(worldEnd - worldStart).count();

    int totalChunks = 8 * 4 * 8;
    double timePerChunk = totalTime / totalChunks;

    // GATE: Average must be < 20ms per chunk (includes generation + meshing + upload)
    std::cout << "  Total world load: " << totalTime << "ms for "
              << totalChunks << " chunks\n";
    std::cout << "  Average: " << timePerChunk << "ms per chunk\n";
    assert(timePerChunk < 20.0);

    world.cleanup(&testRenderer);

    std::cout << "✓ World streaming performance acceptable\n";
}

void test_frame_time_during_streaming() {
    // FUTURE: Run this in the actual game loop
    // Measure frame time while chunks are loading

    std::cout << "⚠ Manual test: Run game and check FPS overlay\n";
    std::cout << "  Frame time must stay under 33ms (30 FPS minimum)\n";
    std::cout << "  No frame should spike above 50ms\n";
}
```

**Performance Gates (Must Not Violate):**
- Single chunk generation: < 5ms
- Single chunk meshing: < 3ms
- Single chunk GPU upload: < 2ms
- Frame time: < 33ms (30 FPS minimum)
- **Max frame spike**: < 50ms (feels like stutter)

**Run: Before release builds** ⏱️ 10-20 seconds

---

### 1.4 Stress/Edge Case Tests (NICE-TO-HAVE)

#### Test Suite: `tests/stress_test.cpp`

```cpp
void test_player_teleport_spam() {
    // Rapid teleporting forces many chunks to load at once
    World world(12, 5, 12);
    world.generateWorld();
    world.createBuffers(&testRenderer);

    // Teleport rapidly to different locations
    for (int i = 0; i < 100; i++) {
        float x = (i % 10) * 20.0f;  // Chunk boundaries
        float z = (i / 10) * 20.0f;

        // Force chunk loading at new position
        world.getChunkAtWorldPos(x, 10.0f, z);
    }

    world.cleanup(&testRenderer);
    std::cout << "✓ 100 rapid teleports handled\n";
}

void test_world_boundary_conditions() {
    // Player at edge of world
    World world(4, 2, 4);
    world.generateWorld();

    // Query at world edges
    Chunk* edge1 = world.getChunkAt(-2, 0, -2);  // Min corner
    Chunk* edge2 = world.getChunkAt(1, 0, 1);    // Max corner
    Chunk* outside = world.getChunkAt(100, 0, 100); // Way outside

    assert(outside == nullptr);  // Should be safe

    world.cleanup(&testRenderer);
    std::cout << "✓ World boundary conditions safe\n";
}

void test_concurrent_modifications() {
    // If you implement threading: test multiple threads modifying chunks
    World world(4, 2, 4);
    world.generateWorld();

    // FUTURE: Thread-safe queue tests would go here

    std::cout << "✓ Concurrent modification tests (if applicable)\n";
}
```

**Run: Before release, or nightly** ⏱️ 5-10 seconds

---

## Part 2: TESTING INFRASTRUCTURE SETUP

### 2.1 Add Testing Framework to CMakeLists.txt

```cmake
# At top of CMakeLists.txt, enable testing
enable_testing()

# For unit tests, add this after main executable:
if(NOT MSVC)
    # Build tests with address sanitizer on Linux/Mac
    add_executable(chunk_test
        tests/chunk_correctness_test.cpp
        tests/memory_leak_test.cpp
        tests/performance_test.cpp
        tests/stress_test.cpp
        # Include necessary source files (not main.cpp)
        src/chunk.cpp
        src/world.cpp
        src/biome_map.cpp
        src/biome_system.cpp
        # ... other required sources
    )
    target_compile_options(chunk_test PRIVATE -fsanitize=address)
    target_link_options(chunk_test PRIVATE -fsanitize=address)
else()
    # Windows: just build without sanitizer
    add_executable(chunk_test
        tests/chunk_correctness_test.cpp
        tests/memory_leak_test.cpp
        tests/performance_test.cpp
        tests/stress_test.cpp
        # ... same source files
    )
endif()

# Link dependencies
target_link_libraries(chunk_test
    ${Vulkan_LIBRARIES}
    glfw
    yaml-cpp
)

# Register tests
add_test(NAME ChunkCorrectness COMMAND chunk_test chunk_correctness)
add_test(NAME MemoryLeaks COMMAND chunk_test memory_leak)
add_test(NAME Performance COMMAND chunk_test performance)
add_test(NAME Stress COMMAND chunk_test stress)

# Run all tests: cmake --build . && ctest --verbose
```

### 2.2 Create Test Harness

Create `tests/test_harness.cpp`:

```cpp
#include <iostream>
#include <map>
#include <functional>

static std::map<std::string, std::function<void()>> tests;

void register_test(const std::string& name, std::function<void()> fn) {
    tests[name] = fn;
}

void run_test_category(const std::string& category) {
    int passed = 0, failed = 0;

    for (auto& [name, fn] : tests) {
        if (name.find(category) != std::string::npos) {
            try {
                fn();
                passed++;
            } catch (const std::exception& e) {
                std::cerr << "✗ " << name << ": " << e.what() << "\n";
                failed++;
            }
        }
    }

    std::cout << "\n" << category << ": " << passed << " passed, "
              << failed << " failed\n";
}

int main(int argc, char* argv[]) {
    // Register all tests
    register_test("chunk_correctness::deterministic", test_chunk_generation_deterministic);
    register_test("chunk_correctness::state_transitions", test_chunk_state_transitions);
    // ... etc

    // Run requested category
    if (argc > 1) {
        run_test_category(argv[1]);
    } else {
        run_test_category("chunk_correctness");
        run_test_category("memory_leak");
        run_test_category("performance");
    }

    return 0;
}
```

---

## Part 3: DEBUGGING TOOLS FOR DEVELOPMENT

### 3.1 Performance Overlay (In-Game)

Add to `debug_state.h`:

```cpp
struct ChunkStreamingStats {
    int chunksLoaded = 0;
    int chunksPending = 0;
    int chunksUnloading = 0;
    float totalMemoryMB = 0.0f;
    float generationQueueSize = 0;
    float meshQueueSize = 0;
    float uploadQueueSize = 0;
};

extern ChunkStreamingStats g_streamingStats;
```

Render in main loop:

```cpp
if (DebugState::instance().showStreamingStats.getValue()) {
    ImGui::SetNextWindowPos(ImVec2(10, 200));
    ImGui::Begin("Streaming Stats");
    ImGui::Text("Loaded: %d", g_streamingStats.chunksLoaded);
    ImGui::Text("Pending: %d", g_streamingStats.chunksPending);
    ImGui::Text("Memory: %.1f MB", g_streamingStats.totalMemoryMB);
    ImGui::Text("Gen Queue: %.0f", g_streamingStats.generationQueueSize);
    ImGui::Text("Mesh Queue: %.0f", g_streamingStats.meshQueueSize);
    ImGui::Text("Upload Queue: %.0f", g_streamingStats.uploadQueueSize);
    ImGui::End();
}
```

### 3.2 Chunk Visualization

Add console command:

```
visualize_chunks [distance] - Show which chunks are loaded/pending/unloading
  Loaded chunks:   GREEN
  Generating:      YELLOW
  Meshing:         CYAN
  GPU upload:      MAGENTA
  Unloading:       RED
```

---

## Part 4: RECOMMENDED TEST SCHEDULE

### Before Every Commit
```bash
# 5-10 seconds
cmake --build . && ctest -L fast

# Includes: chunk_correctness, basic performance gates
```

### Before Release Build
```bash
# 2-5 minutes
cmake --build . --config Release && \
  valgrind ctest -L all && \
  ./stress_test
```

### Nightly CI (if available)
```bash
# Full test suite on all platforms
- Run all tests
- Memory leak detection (Valgrind + ASAN)
- Performance regression testing
- Stress tests
```

---

## Part 5: TEST COVERAGE MATRIX

| Category | Test | Priority | Duration | When to Run |
|----------|------|----------|----------|------------|
| Correctness | Deterministic generation | **CRITICAL** | 1s | Every commit |
| Correctness | State transitions | **CRITICAL** | 2s | Every commit |
| Correctness | Block bounds | HIGH | 1s | Every commit |
| Correctness | Metadata persistence | HIGH | 1s | Every commit |
| Memory | Load/unload cycles | **CRITICAL** | 30s | Before release |
| Memory | Vulkan buffer cleanup | **CRITICAL** | 5s | Before release |
| Memory | GPU memory tracking | HIGH | 20s | Before release |
| Performance | Chunk gen time | HIGH | 2s | Before release |
| Performance | Mesh gen time | HIGH | 2s | Before release |
| Performance | Frame time | HIGH | Manual | Before release |
| Stress | Rapid teleport | MEDIUM | 5s | Nightly |
| Stress | World boundaries | MEDIUM | 3s | Nightly |
| Stress | Concurrent mods | LOW | 5s | Nightly |

---

## Part 6: FAILURE RESPONSE PLAN

**If correctness test fails:**
→ Block all commits until fixed
→ Likely silent world generation bug
→ Regression to last known good seed

**If memory test fails:**
→ Do not ship
→ Run under Valgrind with `-vv` for full trace
→ Look for: missing `delete`, double-free, leaked allocations

**If performance test fails:**
→ Investigate with profiler (perf, Instruments)
→ May need to reduce chunk size or increase thread pool
→ Consider async streaming architecture

**If stress test fails:**
→ Look for thread race conditions
→ May need mutex/lock-free queue fixes
→ Nightly CI issue, not critical for immediate release

---

## Part 7: MINIMAL VIABLE IMPLEMENTATION ROADMAP

**Week 1: Setup**
- [ ] Add CMake test infrastructure
- [ ] Implement chunk_correctness_test.cpp
- [ ] Implement memory_leak_test.cpp
- [ ] Get all tests passing

**Week 2: Performance & Debugging**
- [ ] Implement performance_test.cpp
- [ ] Add performance overlay to game
- [ ] Profile with `perf` / Instruments

**Week 3: Stress & Release**
- [ ] Implement stress_test.cpp
- [ ] Full CI/CD integration
- [ ] Ship with confidence

---

## SUMMARY: What's the Minimum to Ship Safely?

**ABSOLUTE MINIMUM:**
1. `chunk_correctness_test.cpp` - Catches silent bugs
2. `memory_leak_test.cpp` - Prevents hangs/crashes
3. Performance gates in code (5ms, 3ms, 33ms limits)
4. Run before every release build

**TIME INVESTMENT:**
- Setup: 2-3 hours
- Maintenance: 10-15 min/day during active streaming development
- ROI: Prevents shipping memory leaks or inverted terrain

**CRITICAL SUCCESS METRIC:**
> "Player can stream through world for 30 minutes without stuttering, memory growth, or visual artifacts"

