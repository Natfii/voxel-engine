/**
 * @file stress_test.cpp
 * @brief Stress and edge case tests for chunk streaming
 *
 * Tests:
 * 1. Rapid player teleportation (force many chunks to load)
 * 2. World boundary conditions
 * 3. Large number of block modifications
 * 4. Extreme world sizes
 * 5. Edge cases (world at limits, rapid state changes)
 */

#include "test_utils.h"
#include "chunk.h"
#include "world.h"
#include <iostream>

// ============================================================
// Test 1: Rapid Teleportation (Stress)
// ============================================================

TEST(RapidTeleportationStress) {
    Chunk::initNoise(42);

    World world(8, 3, 8);
    world.generateWorld();

    std::cout << "  Simulating 100 rapid teleports...\n";

    // Rapidly teleport to different locations
    for (int i = 0; i < 100; i++) {
        float x = ((i % 10) - 5) * 10.0f;
        float z = ((i / 10) - 5) * 10.0f;

        // Force chunk lookup at new position
        Chunk* chunk = world.getChunkAtWorldPos(x, 10.0f, z);
        // Chunk may be null if out of bounds - that's OK
    }

    world.cleanup(reinterpret_cast<VulkanRenderer*>(&g_testRenderer));
    Chunk::cleanupNoise();

    std::cout << "✓ Rapid teleportation stress test passed\n";
}

// ============================================================
// Test 2: World Boundary Conditions
// ============================================================

TEST(WorldBoundaryConditions) {
    Chunk::initNoise(42);

    World world(4, 2, 4);
    world.generateWorld();

    std::cout << "  Testing boundary access patterns...\n";

    // Query at four corners
    Chunk* c1 = world.getChunkAt(-2, 0, -2);
    Chunk* c2 = world.getChunkAt(1, 0, -2);
    Chunk* c3 = world.getChunkAt(-2, 0, 1);
    Chunk* c4 = world.getChunkAt(1, 0, 1);

    // All should exist
    ASSERT_NOT_NULL(c1);
    ASSERT_NOT_NULL(c2);
    ASSERT_NOT_NULL(c3);
    ASSERT_NOT_NULL(c4);

    // Outside boundaries should be null
    Chunk* outside1 = world.getChunkAt(-3, 0, -3);
    Chunk* outside2 = world.getChunkAt(2, 0, 2);
    Chunk* outside3 = world.getChunkAt(100, 0, 100);
    Chunk* outside4 = world.getChunkAt(-100, 0, -100);

    ASSERT_NULL(outside1);
    ASSERT_NULL(outside2);
    ASSERT_NULL(outside3);
    ASSERT_NULL(outside4);

    std::cout << "  ✓ Boundary conditions safe\n";

    // Test world-space boundary queries
    int block1 = world.getBlockAt(-100.0f, 10.0f, -100.0f);  // Should be 0 (out of bounds)
    int block2 = world.getBlockAt(100.0f, 10.0f, 100.0f);     // Should be 0 (out of bounds)

    // Blocks at valid positions should be non-zero if in solid terrain
    // Can't assert specific values, just that we don't crash

    world.cleanup(reinterpret_cast<VulkanRenderer*>(&g_testRenderer));
    Chunk::cleanupNoise();

    std::cout << "✓ World boundary conditions test passed\n";
}

// ============================================================
// Test 3: Massive Block Modification
// ============================================================

TEST(MassiveBlockModification) {
    Chunk::initNoise(42);

    World world(4, 2, 4);
    world.generateWorld();

    std::cout << "  Modifying 10000 blocks...\n";

    // Modify lots of blocks
    for (int i = 0; i < 10000; i++) {
        float x = ((i % 20) - 10) * 1.6f;
        float y = ((i / 20) % 20) * 1.6f;
        float z = ((i / 400) - 5) * 1.6f;

        world.setBlockAt(x, y, z, 1);

        if (i % 1000 == 0) {
            std::cout << "    Modified " << i << "/10000\n";
        }
    }

    // Verify some modifications
    for (int i = 0; i < 100; i++) {
        float x = ((i % 20) - 10) * 1.6f;
        float y = ((i / 20) % 20) * 1.6f;
        float z = ((i / 400) - 5) * 1.6f;

        int block = world.getBlockAt(x, y, z);
        // Just verify it doesn't crash
    }

    world.cleanup(reinterpret_cast<VulkanRenderer*>(&g_testRenderer));
    Chunk::cleanupNoise();

    std::cout << "✓ Massive block modification test passed\n";
}

// ============================================================
// Test 4: Extreme World Size
// ============================================================

TEST(ExtremeWorldSize) {
    Chunk::initNoise(42);

    std::cout << "  Creating large world (10x4x10 = 400 chunks)...\n";

    World world(10, 4, 10);
    world.generateWorld();

    // Query chunks across the entire world
    for (int x = -5; x < 5; x++) {
        for (int z = -5; z < 5; z++) {
            Chunk* chunk = world.getChunkAt(x, 0, z);
            ASSERT_NOT_NULL(chunk);
        }
    }

    world.cleanup(reinterpret_cast<VulkanRenderer*>(&g_testRenderer));
    Chunk::cleanupNoise();

    std::cout << "✓ Extreme world size test passed\n";
}

// ============================================================
// Test 5: Rapid Chunk State Changes
// ============================================================

TEST(RapidChunkStateChanges) {
    Chunk::initNoise(42);

    std::cout << "  Rapidly changing chunk states...\n";

    Chunk c(5, 5, 5);
    MockBiomeMap biomeMap;

    for (int iteration = 0; iteration < 50; iteration++) {
        // Generate
        c.generate(&biomeMap);

        // Modify blocks
        for (int i = 0; i < 50; i++) {
            int x = (i % 8);
            int y = (i / 8) % 4;
            int z = (i / 32) % 1;
            c.setBlock(x, y, z, iteration % 10);
        }

        // Generate mesh (but no world context for full mesh, just call it)
        // Note: Full mesh generation needs a world context, skip for stress test
    }

    std::cout << "✓ Rapid chunk state changes test passed\n";

    Chunk::cleanupNoise();
}

// ============================================================
// Test 6: Metadata Stress
// ============================================================

TEST(MetadataStress) {
    Chunk c(0, 0, 0);

    std::cout << "  Setting and checking 10000 metadata values...\n";

    // Set lots of metadata
    for (int i = 0; i < 10000; i++) {
        int x = (i * 7) % 32;
        int y = (i * 11) % 32;
        int z = (i * 13) % 32;
        uint8_t value = i % 256;

        c.setBlockMetadata(x, y, z, value);
    }

    // Verify a sample
    for (int i = 0; i < 100; i++) {
        int x = (i * 7) % 32;
        int y = (i * 11) % 32;
        int z = (i * 13) % 32;
        uint8_t expected = i % 256;

        uint8_t actual = c.getBlockMetadata(x, y, z);
        ASSERT_EQ(actual, expected);
    }

    std::cout << "✓ Metadata stress test passed\n";
}

// ============================================================
// Test 7: Overlapping Block Modifications
// ============================================================

TEST(OverlappingBlockModifications) {
    Chunk::initNoise(42);

    World world(4, 2, 4);
    world.generateWorld();

    std::cout << "  Modifying same blocks repeatedly...\n";

    // Repeatedly modify the same blocks
    for (int iteration = 0; iteration < 100; iteration++) {
        for (int x = 0; x < 10; x++) {
            for (int z = 0; z < 10; z++) {
                float worldX = x * 1.6f;
                float worldZ = z * 1.6f;
                float worldY = 10.0f;

                // Set to different block types
                int blockID = (iteration + x + z) % 5 + 1;
                world.setBlockAt(worldX, worldY, worldZ, blockID);
            }
        }
    }

    // Verify final state
    for (int x = 0; x < 10; x++) {
        for (int z = 0; z < 10; z++) {
            float worldX = x * 1.6f;
            float worldZ = z * 1.6f;
            float worldY = 10.0f;

            int block = world.getBlockAt(worldX, worldY, worldZ);
            // Just verify it doesn't crash
        }
    }

    world.cleanup(reinterpret_cast<VulkanRenderer*>(&g_testRenderer));
    Chunk::cleanupNoise();

    std::cout << "✓ Overlapping block modifications test passed\n";
}

// ============================================================
// Test 8: Chunk Access Pattern Stress
// ============================================================

TEST(ChunkAccessPatternStress) {
    Chunk::initNoise(42);

    World world(6, 3, 6);
    world.generateWorld();

    std::cout << "  Testing various access patterns...\n";

    // Linear sweep
    for (int x = -3; x < 3; x++) {
        for (int z = -3; z < 3; z++) {
            world.getChunkAt(x, 0, z);
        }
    }

    // Random-ish pattern
    for (int i = 0; i < 100; i++) {
        int x = ((i * 13) % 7) - 3;
        int z = ((i * 17) % 7) - 3;
        world.getChunkAt(x, 0, z);
    }

    // Spiral pattern
    for (int radius = 0; radius < 5; radius++) {
        for (int x = -radius; x <= radius; x++) {
            for (int z = -radius; z <= radius; z++) {
                if (std::abs(x) == radius || std::abs(z) == radius) {
                    world.getChunkAt(x, 0, z);
                }
            }
        }
    }

    world.cleanup(reinterpret_cast<VulkanRenderer*>(&g_testRenderer));
    Chunk::cleanupNoise();

    std::cout << "✓ Chunk access pattern stress test passed\n";
}

// ============================================================
// Main Entry Point
// ============================================================

int main() {
    try {
        std::cout << "========================================\n";
        std::cout << "STRESS AND EDGE CASE TESTS\n";
        std::cout << "========================================\n\n";

        run_all_tests();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "TEST FAILURE: " << e.what() << std::endl;
        return 1;
    }
}
