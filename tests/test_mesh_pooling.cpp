/**
 * @file test_mesh_pooling.cpp
 * @brief Performance test for mesh buffer pooling optimization
 *
 * EXPECTED RESULTS:
 * - Without pooling: ~100-150ms for 1000 allocations
 * - With pooling: ~40-60ms for 1000 allocations (40-60% speedup)
 *
 * Created: 2025-11-14
 */

#include "mesh_buffer_pool.h"
#include "chunk.h"
#include <chrono>
#include <iostream>
#include <vector>

/**
 * @brief Simulates mesh generation without pooling (direct allocation)
 */
double benchmarkWithoutPooling(int iterations) {
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; ++i) {
        // Simulate typical chunk mesh generation
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        vertices.reserve(2000);  // Typical chunk has ~500-2000 vertices
        indices.reserve(4000);

        // Simulate adding vertices (typical workload)
        for (int j = 0; j < 1500; ++j) {
            vertices.push_back({0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f});
        }

        for (int j = 0; j < 3000; ++j) {
            indices.push_back(j);
        }

        // Vectors go out of scope and deallocate
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    return duration.count() / 1000.0;  // Convert to milliseconds
}

/**
 * @brief Simulates mesh generation with pooling (reuses allocations)
 */
double benchmarkWithPooling(int iterations) {
    MeshBufferPool pool(16);  // Pre-allocate 16 buffers

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; ++i) {
        // Acquire buffers from pool
        std::vector<Vertex> vertices = pool.acquireVertexBuffer();
        std::vector<uint32_t> indices = pool.acquireIndexBuffer();

        vertices.reserve(2000);
        indices.reserve(4000);

        // Simulate adding vertices
        for (int j = 0; j < 1500; ++j) {
            vertices.push_back({0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f});
        }

        for (int j = 0; j < 3000; ++j) {
            indices.push_back(j);
        }

        // Return buffers to pool for reuse
        pool.releaseVertexBuffer(std::move(vertices));
        pool.releaseIndexBuffer(std::move(indices));
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    return duration.count() / 1000.0;  // Convert to milliseconds
}

int main() {
    const int ITERATIONS = 1000;
    const int WARMUP_ITERATIONS = 100;

    std::cout << "=== Mesh Buffer Pooling Performance Test ===" << std::endl;
    std::cout << "Testing with " << ITERATIONS << " iterations\n" << std::endl;

    // Warmup
    std::cout << "Warming up..." << std::endl;
    benchmarkWithoutPooling(WARMUP_ITERATIONS);
    benchmarkWithPooling(WARMUP_ITERATIONS);

    // Benchmark without pooling
    std::cout << "\n[1/2] Running WITHOUT pooling..." << std::endl;
    double timeWithoutPooling = benchmarkWithoutPooling(ITERATIONS);
    std::cout << "Time: " << timeWithoutPooling << " ms" << std::endl;

    // Benchmark with pooling
    std::cout << "\n[2/2] Running WITH pooling..." << std::endl;
    double timeWithPooling = benchmarkWithPooling(ITERATIONS);
    std::cout << "Time: " << timeWithPooling << " ms" << std::endl;

    // Calculate speedup
    double speedup = ((timeWithoutPooling - timeWithPooling) / timeWithoutPooling) * 100.0;

    std::cout << "\n=== Results ===" << std::endl;
    std::cout << "Without pooling: " << timeWithoutPooling << " ms" << std::endl;
    std::cout << "With pooling:    " << timeWithPooling << " ms" << std::endl;
    std::cout << "Speedup:         " << speedup << "%" << std::endl;

    if (speedup >= 40.0) {
        std::cout << "\n✓ SUCCESS: Achieved target 40-60% speedup!" << std::endl;
        return 0;
    } else if (speedup >= 20.0) {
        std::cout << "\n⚠ PARTIAL: Speedup is positive but below target" << std::endl;
        return 0;
    } else {
        std::cout << "\n✗ FAILURE: Speedup below expectations" << std::endl;
        return 1;
    }
}
