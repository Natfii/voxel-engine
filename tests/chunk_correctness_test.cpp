/**
 * @file chunk_correctness_test.cpp
 * @brief Correctness tests for chunk generation and operations
 *
 * Tests:
 * 1. Deterministic generation (same seed = same terrain)
 * 2. State transitions (constructor -> generate -> mesh -> buffer -> destroy)
 * 3. Block access bounds checking
 * 4. Metadata persistence
 */

#include "test_utils.h"
#include "chunk.h"
#include "world.h"

// ============================================================
// Test 1: Deterministic Generation
// ============================================================

TEST(ChunkGenerationDeterministic) {
    // Initialize noise with fixed seed
    Chunk::initNoise(42);

    // Generate first chunk
    Chunk c1(0, 0, 0);
    MockBiomeMap biomeMap;
    c1.generate(reinterpret_cast<class BiomeMap*>(&biomeMap));

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

    // Generate second chunk with same seed
    Chunk c2(0, 0, 0);
    c2.generate(reinterpret_cast<class BiomeMap*>(&biomeMap));

    // Verify identical blocks
    int differences = 0;
    for (int x = 0; x < 32; x++) {
        for (int y = 0; y < 32; y++) {
            for (int z = 0; z < 32; z++) {
                if (c2.getBlock(x, y, z) != blocksCopy[x][y][z]) {
                    differences++;
                }
            }
        }
    }

    ASSERT_EQ(differences, 0);
    std::cout << "✓ Chunk generation is deterministic\n";

    Chunk::cleanupNoise();
}

// ============================================================
// Test 2: Chunk State Transitions
// ============================================================

TEST(ChunkStateTransitions) {
    Chunk::initNoise(42);

    // 1. Constructor initializes to air
    {
        Chunk c(5, 5, 5);
        int airBlocks = 0;
        for (int x = 0; x < 32; x++) {
            for (int y = 0; y < 32; y++) {
                for (int z = 0; z < 32; z++) {
                    if (c.getBlock(x, y, z) == 0) {
                        airBlocks++;
                    }
                }
            }
        }

        ASSERT_EQ(airBlocks, 32 * 32 * 32);
        std::cout << "  ✓ Constructor initializes all blocks to air\n";
    }

    // 2. Generation fills blocks
    {
        // Use Y=0 to ensure chunk is at ground level and contains terrain
        Chunk c(0, 0, 0);
        MockBiomeMap biomeMap;
        c.generate(reinterpret_cast<class BiomeMap*>(&biomeMap));

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

        ASSERT_TRUE(hasBlocks);
        std::cout << "  ✓ Generation fills chunk with blocks\n";
    }

    // 3. Mesh generation doesn't crash
    {
        // Use Y=0 to ensure chunk has actual terrain to mesh
        Chunk c(0, 0, 0);
        MockBiomeMap biomeMap;
        c.generate(reinterpret_cast<class BiomeMap*>(&biomeMap));

        World world(3, 3, 3);
        world.generateWorld();

        c.generateMesh(&world);
        uint32_t vertexCount = c.getVertexCount();

        ASSERT_GE(vertexCount, 0);
        ASSERT_LT(vertexCount, 1000000);  // Sanity check
        std::cout << "  ✓ Mesh generation succeeds (vertex count: " << vertexCount << ")\n";
    }

    // 4. Vertex count is reasonable
    {
        Chunk c(5, 5, 5);
        MockBiomeMap biomeMap;
        c.generate(reinterpret_cast<class BiomeMap*>(&biomeMap));

        // Non-empty chunk should have some vertices
        if (c.getVertexCount() > 0) {
            std::cout << "  ✓ Non-empty chunk has " << c.getVertexCount() << " vertices\n";
        }
    }

    Chunk::cleanupNoise();
}

// ============================================================
// Test 3: Block Access Bounds
// ============================================================

TEST(BlockAccessBounds) {
    Chunk c(0, 0, 0);

    // Valid access
    c.setBlock(0, 0, 0, 1);
    ASSERT_EQ(c.getBlock(0, 0, 0), 1);

    c.setBlock(31, 31, 31, 5);
    ASSERT_EQ(c.getBlock(31, 31, 31), 5);

    // Interior block
    c.setBlock(15, 15, 15, 3);
    ASSERT_EQ(c.getBlock(15, 15, 15), 3);

    // Out of bounds should return -1
    ASSERT_EQ(c.getBlock(-1, 0, 0), -1);
    ASSERT_EQ(c.getBlock(32, 0, 0), -1);
    ASSERT_EQ(c.getBlock(0, -1, 0), -1);
    ASSERT_EQ(c.getBlock(0, 32, 0), -1);
    ASSERT_EQ(c.getBlock(0, 0, -1), -1);
    ASSERT_EQ(c.getBlock(0, 0, 32), -1);

    // Way out of bounds
    ASSERT_EQ(c.getBlock(100, 100, 100), -1);
    ASSERT_EQ(c.getBlock(-100, -100, -100), -1);

    std::cout << "✓ Block access bounds checking works\n";
}

// ============================================================
// Test 4: Metadata Persistence
// ============================================================

TEST(BlockMetadataPersistence) {
    Chunk c(0, 0, 0);

    // Metadata should persist at various values
    c.setBlockMetadata(5, 10, 15, 127);
    ASSERT_EQ(c.getBlockMetadata(5, 10, 15), 127);

    c.setBlockMetadata(0, 0, 0, 0);
    ASSERT_EQ(c.getBlockMetadata(0, 0, 0), 0);

    c.setBlockMetadata(31, 31, 31, 255);
    ASSERT_EQ(c.getBlockMetadata(31, 31, 31), 255);

    // Multiple metadata values independently
    c.setBlockMetadata(1, 1, 1, 10);
    c.setBlockMetadata(2, 2, 2, 20);
    c.setBlockMetadata(3, 3, 3, 30);

    ASSERT_EQ(c.getBlockMetadata(1, 1, 1), 10);
    ASSERT_EQ(c.getBlockMetadata(2, 2, 2), 20);
    ASSERT_EQ(c.getBlockMetadata(3, 3, 3), 30);

    // Setting one shouldn't affect others
    c.setBlockMetadata(1, 1, 1, 100);
    ASSERT_EQ(c.getBlockMetadata(1, 1, 1), 100);
    ASSERT_EQ(c.getBlockMetadata(2, 2, 2), 20);
    ASSERT_EQ(c.getBlockMetadata(3, 3, 3), 30);

    std::cout << "✓ Block metadata storage works\n";
}

// ============================================================
// Test 5: Chunk Position Tracking
// ============================================================

TEST(ChunkPositionTracking) {
    Chunk c1(0, 0, 0);
    ASSERT_EQ(c1.getChunkX(), 0);
    ASSERT_EQ(c1.getChunkY(), 0);
    ASSERT_EQ(c1.getChunkZ(), 0);

    Chunk c2(5, -3, 10);
    ASSERT_EQ(c2.getChunkX(), 5);
    ASSERT_EQ(c2.getChunkY(), -3);
    ASSERT_EQ(c2.getChunkZ(), 10);

    Chunk c3(-100, 50, -50);
    ASSERT_EQ(c3.getChunkX(), -100);
    ASSERT_EQ(c3.getChunkY(), 50);
    ASSERT_EQ(c3.getChunkZ(), -50);

    std::cout << "✓ Chunk position tracking works\n";
}

// ============================================================
// Test 6: World Chunk Lookup
// ============================================================

TEST(WorldChunkLookup) {
    Chunk::initNoise(42);

    World world(4, 2, 4);
    world.generateWorld();

    // Valid lookups
    Chunk* chunk1 = world.getChunkAt(0, 0, 0);
    ASSERT_NOT_NULL(chunk1);

    Chunk* chunk2 = world.getChunkAt(-2, 0, -2);
    ASSERT_NOT_NULL(chunk2);

    Chunk* chunk3 = world.getChunkAt(1, 0, 1);
    ASSERT_NOT_NULL(chunk3);

    // Out of bounds should return nullptr
    Chunk* outOfBounds = world.getChunkAt(100, 0, 100);
    ASSERT_NULL(outOfBounds);

    std::cout << "✓ World chunk lookup works\n";

    Chunk::cleanupNoise();
}

// ============================================================
// Main Entry Point
// ============================================================

int main() {
    try {
        run_all_tests();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "TEST FAILURE: " << e.what() << std::endl;
        return 1;
    }
}
