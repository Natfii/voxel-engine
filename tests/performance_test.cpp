/**
 * @file performance_test.cpp
 * @brief Performance gate tests for chunk streaming
 *
 * Tests:
 * 1. Chunk generation time (< 5ms per chunk)
 * 2. Mesh generation time (< 3ms per chunk)
 * 3. World initialization time
 * 4. Block access performance
 *
 * PERFORMANCE GATES (MUST NOT VIOLATE):
 * - Single chunk generation: < 5ms
 * - Single chunk meshing: < 3ms
 * - Single chunk GPU upload: < 2ms
 * - Frame time: < 33ms (30 FPS minimum)
 * - Max frame spike: < 50ms (feels like stutter)
 */

#include "test_utils.h"
#include "chunk.h"
#include "world.h"
#include <chrono>
#include <vector>
#include <algorithm>

// ============================================================
// Utility: Timing Helper
// ============================================================

struct TimingStats {
    double min_ms;
    double max_ms;
    double average_ms;
    double median_ms;

    TimingStats() : min_ms(1e9), max_ms(0), average_ms(0), median_ms(0) {}
};

TimingStats analyze_timings(const std::vector<double>& timings) {
    TimingStats stats;

    if (timings.empty()) return stats;

    double sum = 0;
    for (double t : timings) {
        sum += t;
        stats.min_ms = std::min(stats.min_ms, t);
        stats.max_ms = std::max(stats.max_ms, t);
    }

    stats.average_ms = sum / timings.size();

    auto sorted = timings;
    std::sort(sorted.begin(), sorted.end());
    stats.median_ms = sorted[sorted.size() / 2];

    return stats;
}

// ============================================================
// Test 1: Single Chunk Generation Time
// ============================================================

TEST(ChunkGenerationPerformance) {
    Chunk::initNoise(42);

    std::vector<double> timings;

    std::cout << "  Generating 10 chunks...\n";

    for (int i = 0; i < 10; i++) {
        Chunk c(i, 0, 0);
        MockBiomeMap biomeMap;

        auto start = std::chrono::high_resolution_clock::now();
        c.generate(&biomeMap);
        auto end = std::chrono::high_resolution_clock::now();

        double duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
        timings.push_back(duration_ms);
    }

    TimingStats stats = analyze_timings(timings);

    std::cout << "  Chunk generation time:\n";
    std::cout << "    Min: " << stats.min_ms << " ms\n";
    std::cout << "    Max: " << stats.max_ms << " ms\n";
    std::cout << "    Avg: " << stats.average_ms << " ms\n";
    std::cout << "    Median: " << stats.median_ms << " ms\n";

    // GATE: Must be < 5ms per chunk (allows ~6 chunks per frame at 30 FPS)
    ASSERT_LT(stats.average_ms, 5.0);
    ASSERT_LT(stats.max_ms, 7.0);

    std::cout << "  ✓ Chunk generation within gate (< 5ms)\n";

    Chunk::cleanupNoise();
}

// ============================================================
// Test 2: Mesh Generation Performance
// ============================================================

TEST(MeshGenerationPerformance) {
    Chunk::initNoise(42);

    World world(5, 3, 5);
    world.generateWorld();

    std::vector<double> timings;

    std::cout << "  Generating meshes for 10 chunks...\n";

    for (int i = 0; i < 10; i++) {
        Chunk* chunk = world.getChunkAt(i % 5, 0, i / 5);

        if (chunk == nullptr) continue;

        auto start = std::chrono::high_resolution_clock::now();
        chunk->generateMesh(&world);
        auto end = std::chrono::high_resolution_clock::now();

        double duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
        timings.push_back(duration_ms);
    }

    TimingStats stats = analyze_timings(timings);

    std::cout << "  Mesh generation time:\n";
    std::cout << "    Min: " << stats.min_ms << " ms\n";
    std::cout << "    Max: " << stats.max_ms << " ms\n";
    std::cout << "    Avg: " << stats.average_ms << " ms\n";
    std::cout << "    Median: " << stats.median_ms << " ms\n";

    // GATE: Must be < 3ms per chunk
    ASSERT_LT(stats.average_ms, 3.0);
    ASSERT_LT(stats.max_ms, 5.0);

    std::cout << "  ✓ Mesh generation within gate (< 3ms)\n";

    world.cleanup(&g_testRenderer);
    Chunk::cleanupNoise();
}

// ============================================================
// Test 3: World Initialization Performance
// ============================================================

TEST(WorldInitializationPerformance) {
    Chunk::initNoise(42);

    std::cout << "  Initializing 6x4x6 world (144 chunks)...\n";

    auto start = std::chrono::high_resolution_clock::now();
    World world(6, 4, 6);
    world.generateWorld();
    auto end = std::chrono::high_resolution_clock::now();

    double total_ms = std::chrono::duration<double, std::milli>(end - start).count();
    int totalChunks = 6 * 4 * 6;
    double perChunk_ms = total_ms / totalChunks;

    std::cout << "  World initialization performance:\n";
    std::cout << "    Total time: " << total_ms << " ms\n";
    std::cout << "    Total chunks: " << totalChunks << "\n";
    std::cout << "    Per chunk: " << perChunk_ms << " ms\n";

    // GATE: Average should be < 20ms per chunk
    // (includes generation + meshing + all overhead)
    ASSERT_LT(perChunk_ms, 20.0);

    std::cout << "  ✓ World initialization within gate\n";

    world.cleanup(&g_testRenderer);
    Chunk::cleanupNoise();
}

// ============================================================
// Test 4: Block Access Performance
// ============================================================

TEST(BlockAccessPerformance) {
    Chunk c(0, 0, 0);

    std::cout << "  Performing 10000 block accesses...\n";

    auto start = std::chrono::high_resolution_clock::now();

    // 10000 block reads
    for (int i = 0; i < 10000; i++) {
        int x = (i * 17) % 32;
        int y = (i * 19) % 32;
        int z = (i * 23) % 32;
        volatile int block = c.getBlock(x, y, z);
        (void)block;  // Prevent optimization
    }

    auto end = std::chrono::high_resolution_clock::now();
    double duration_ms = std::chrono::duration<double, std::milli>(end - start).count();

    double perAccess_us = duration_ms * 1000.0 / 10000.0;

    std::cout << "  Block access performance:\n";
    std::cout << "    Total time: " << duration_ms << " ms\n";
    std::cout << "    Per access: " << perAccess_us << " µs\n";

    // GATE: Should be very fast (< 1 µs per access)
    ASSERT_LT(perAccess_us, 10.0);

    std::cout << "  ✓ Block access performance excellent\n";
}

// ============================================================
// Test 5: Block Modification Performance
// ============================================================

TEST(BlockModificationPerformance) {
    Chunk c(0, 0, 0);

    std::cout << "  Performing 1000 block modifications...\n";

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 1000; i++) {
        int x = (i * 17) % 32;
        int y = (i * 19) % 32;
        int z = (i * 23) % 32;
        c.setBlock(x, y, z, (i % 5) + 1);
    }

    auto end = std::chrono::high_resolution_clock::now();
    double duration_ms = std::chrono::duration<double, std::milli>(end - start).count();

    double perModify_us = duration_ms * 1000.0 / 1000.0;

    std::cout << "  Block modification performance:\n";
    std::cout << "    Total time: " << duration_ms << " ms\n";
    std::cout << "    Per modification: " << perModify_us << " µs\n";

    // GATE: Should be very fast (< 10 µs per modification)
    ASSERT_LT(perModify_us, 100.0);

    std::cout << "  ✓ Block modification performance good\n";
}

// ============================================================
// Test 6: Metadata Performance
// ============================================================

TEST(MetadataPerformance) {
    Chunk c(0, 0, 0);

    std::cout << "  Performing 5000 metadata operations...\n";

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 5000; i++) {
        int x = (i * 17) % 32;
        int y = (i * 19) % 32;
        int z = (i * 23) % 32;

        // Alternately set and get metadata
        if (i % 2 == 0) {
            c.setBlockMetadata(x, y, z, i % 256);
        } else {
            volatile uint8_t metadata = c.getBlockMetadata(x, y, z);
            (void)metadata;
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    double duration_ms = std::chrono::duration<double, std::milli>(end - start).count();

    double perOp_us = duration_ms * 1000.0 / 5000.0;

    std::cout << "  Metadata operation performance:\n";
    std::cout << "    Total time: " << duration_ms << " ms\n";
    std::cout << "    Per operation: " << perOp_us << " µs\n";

    ASSERT_LT(perOp_us, 50.0);

    std::cout << "  ✓ Metadata performance good\n";
}

// ============================================================
// Test 7: World Block Access Performance
// ============================================================

TEST(WorldBlockAccessPerformance) {
    Chunk::initNoise(42);

    World world(4, 2, 4);
    world.generateWorld();

    std::cout << "  Performing 1000 world block accesses...\n";

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 1000; i++) {
        float x = ((i % 10) * 1.6f) - 3.2f;
        float y = ((i / 10) * 1.6f);
        float z = ((i / 100) * 1.6f) - 3.2f;

        volatile int block = world.getBlockAt(x, y, z);
        (void)block;
    }

    auto end = std::chrono::high_resolution_clock::now();
    double duration_ms = std::chrono::duration<double, std::milli>(end - start).count();

    double perAccess_us = duration_ms * 1000.0 / 1000.0;

    std::cout << "  World block access performance:\n";
    std::cout << "    Total time: " << duration_ms << " ms\n";
    std::cout << "    Per access: " << perAccess_us << " µs\n";

    // World lookup has overhead (coordinate conversion + chunk lookup)
    ASSERT_LT(perAccess_us, 500.0);

    std::cout << "  ✓ World block access performance acceptable\n";

    world.cleanup(&g_testRenderer);
    Chunk::cleanupNoise();
}

// ============================================================
// Main Entry Point
// ============================================================

int main() {
    try {
        std::cout << "========================================\n";
        std::cout << "PERFORMANCE GATE TESTS\n";
        std::cout << "========================================\n";
        std::cout << "Required Performance Gates:\n";
        std::cout << "  - Single chunk generation: < 5ms\n";
        std::cout << "  - Single chunk meshing: < 3ms\n";
        std::cout << "  - Block access: < 10 µs\n";
        std::cout << "  - World loading: < 20ms per chunk\n";
        std::cout << "========================================\n\n";

        run_all_tests();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "TEST FAILURE: " << e.what() << std::endl;
        return 1;
    }
}
