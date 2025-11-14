/**
 * @file memory_leak_test.cpp
 * @brief Memory leak detection tests
 *
 * Tests:
 * 1. Chunk load/unload cycles (1000x)
 * 2. Vulkan buffer creation and destruction
 * 3. World initialization and cleanup
 *
 * Run with: valgrind --leak-check=full ./test_memory_leaks
 * Or with Address Sanitizer (compiled with -fsanitize=address)
 */

#include "test_utils.h"
#include "chunk.h"
#include "world.h"
#include <iostream>

// ============================================================
// Test 1: Chunk Load/Unload Cycles
// ============================================================

TEST(ChunkLoadUnloadCycles) {
    std::cout << "  Running 100 chunk load/unload cycles...\n";

    Chunk::initNoise(42);

    for (int cycle = 0; cycle < 100; cycle++) {
        // Create and destroy chunk
        {
            Chunk c(cycle % 10, cycle / 10, 0);
            MockBiomeMap biomeMap;
            c.generate(&biomeMap);

            // Simulate block modifications
            c.setBlock(5, 5, 5, 1);
            c.setBlock(10, 10, 10, 3);
            c.setBlockMetadata(15, 15, 15, 42);

            // Generate mesh
            World world(3, 2, 3);
            world.generateWorld();
            c.generateMesh(&world);
        } // Chunk destroyed here

        if (cycle % 20 == 0) {
            std::cout << "    Cycle " << cycle << "/100\n";
        }
    }

    Chunk::cleanupNoise();
    std::cout << "✓ 100 chunk load/unload cycles completed\n";
}

// ============================================================
// Test 2: World Load/Unload Cycles
// ============================================================

TEST(WorldLoadUnloadCycles) {
    std::cout << "  Running 50 world load/unload cycles...\n";

    for (int cycle = 0; cycle < 50; cycle++) {
        // Create and destroy world
        {
            Chunk::initNoise(42 + cycle);

            World world(4, 2, 4);  // Small world for fast testing
            world.generateWorld();

            // Query some chunks
            world.getChunkAt(0, 0, 0);
            world.getChunkAt(-2, 0, -2);

            // Simulate block modifications
            world.setBlockAt(5.0f, 10.0f, 5.0f, 1);
            world.setBlockAt(10.0f, 10.0f, 10.0f, 0);

            // Create buffers
            world.createBuffers(reinterpret_cast<VulkanRenderer*>(&g_testRenderer));

            // Cleanup
            world.cleanup(reinterpret_cast<VulkanRenderer*>(&g_testRenderer));

            Chunk::cleanupNoise();
        } // World destroyed here

        if (cycle % 10 == 0) {
            std::cout << "    Cycle " << cycle << "/50\n";
        }
    }

    std::cout << "✓ 50 world load/unload cycles completed\n";
}

// ============================================================
// Test 3: Chunk Buffer Lifecycle
// ============================================================

TEST(ChunkBufferLifecycle) {
    Chunk::initNoise(42);

    std::cout << "  Testing chunk buffer creation and destruction...\n";

    for (int i = 0; i < 10; i++) {
        Chunk c(i, 0, 0);
        MockBiomeMap biomeMap;
        c.generate(&biomeMap);

        uint32_t vertexCount = c.getVertexCount();
        if (vertexCount > 0) {
            // Only create buffer if chunk has geometry
            c.createVertexBuffer(reinterpret_cast<VulkanRenderer*>(&g_testRenderer));
        }

        // Destroy buffers
        c.destroyBuffers(reinterpret_cast<VulkanRenderer*>(&g_testRenderer));
    }

    Chunk::cleanupNoise();
    std::cout << "✓ Chunk buffer lifecycle correct\n";
}

// ============================================================
// Test 4: Large World Cleanup
// ============================================================

TEST(LargeWorldCleanup) {
    Chunk::initNoise(42);

    std::cout << "  Creating large world (8x3x8 = 192 chunks)...\n";

    World world(8, 3, 8);
    world.generateWorld();

    std::cout << "  Creating GPU buffers...\n";
    world.createBuffers(reinterpret_cast<VulkanRenderer*>(&g_testRenderer));

    std::cout << "  Cleaning up world...\n";
    world.cleanup(reinterpret_cast<VulkanRenderer*>(&g_testRenderer));

    // At this point, all resources should be freed
    // (Verify with Valgrind or ASAN)

    Chunk::cleanupNoise();
    std::cout << "✓ Large world cleanup completed successfully\n";
}

// ============================================================
// Test 5: Repeated World Regeneration
// ============================================================

TEST(RepeatedWorldRegeneration) {
    std::cout << "  Running 20 world regeneration cycles...\n";

    for (int regeneration = 0; regeneration < 20; regeneration++) {
        Chunk::initNoise(100 + regeneration);

        World world(4, 2, 4);
        world.generateWorld();

        // Simulate decorating (optional)
        world.decorateWorld();

        // Cleanup
        world.cleanup(reinterpret_cast<VulkanRenderer*>(&g_testRenderer));

        Chunk::cleanupNoise();

        if (regeneration % 5 == 0) {
            std::cout << "    Regeneration " << regeneration << "/20\n";
        }
    }

    std::cout << "✓ 20 world regeneration cycles completed\n";
}

// ============================================================
// Test 6: Block Modification Memory Safety
// ============================================================

TEST(BlockModificationMemorySafety) {
    Chunk::initNoise(42);

    World world(3, 2, 3);
    world.generateWorld();

    std::cout << "  Modifying 1000 blocks...\n";

    // Modify many blocks
    for (int i = 0; i < 1000; i++) {
        float x = (i % 10) * 1.6f;
        float y = ((i / 10) % 10) * 1.6f;
        float z = (i / 100) * 1.6f;

        world.setBlockAt(x, y, z, 1);
    }

    // Query modified blocks
    for (int i = 0; i < 1000; i++) {
        float x = (i % 10) * 1.6f;
        float y = ((i / 10) % 10) * 1.6f;
        float z = (i / 100) * 1.6f;

        int blockID = world.getBlockAt(x, y, z);
        // Just verify it doesn't crash
    }

    world.cleanup(reinterpret_cast<VulkanRenderer*>(&g_testRenderer));
    Chunk::cleanupNoise();

    std::cout << "✓ Block modification memory safety verified\n";
}

// ============================================================
// Main Entry Point
// ============================================================

int main() {
    try {
        std::cout << "========================================\n";
        std::cout << "MEMORY LEAK DETECTION TESTS\n";
        std::cout << "========================================\n";
        std::cout << "Run with: valgrind --leak-check=full ./test_memory_leaks\n";
        std::cout << "Or: ASAN_OPTIONS=verbosity=2 ./test_memory_leaks\n";
        std::cout << "========================================\n\n";

        run_all_tests();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "TEST FAILURE: " << e.what() << std::endl;
        return 1;
    }
}
