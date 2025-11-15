/**
 * Test file for biome selection noise system
 *
 * This test validates:
 * 1. Noise values are in expected ranges
 * 2. Biome selection is continuous across world positions
 * 3. Biomes span multiple chunks (large-scale features)
 * 4. Multiple noise layers create variety
 */

#include "biome_map.h"
#include "biome_system.h"
#include <iostream>
#include <unordered_map>
#include <cmath>
#include <iomanip>

// Test configuration
constexpr int TEST_SEED = 12345;
constexpr int SAMPLE_DISTANCE = 10;  // Sample every 10 blocks
constexpr int TEST_AREA_SIZE = 1000; // 1000x1000 block area

void testNoiseRanges() {
    std::cout << "\n=== Testing Noise Value Ranges ===\n";

    BiomeMap biomeMap(TEST_SEED);

    // Sample noise values across a large area
    float minTemp = 100.0f, maxTemp = 0.0f;
    float minMoisture = 100.0f, maxMoisture = 0.0f;
    float minWeirdness = 100.0f, maxWeirdness = 0.0f;
    float minErosion = 100.0f, maxErosion = 0.0f;

    for (int x = 0; x < TEST_AREA_SIZE; x += SAMPLE_DISTANCE) {
        for (int z = 0; z < TEST_AREA_SIZE; z += SAMPLE_DISTANCE) {
            float temp = biomeMap.getTemperatureAt(x, z);
            float moisture = biomeMap.getMoistureAt(x, z);
            float weirdness = biomeMap.getWeirdnessAt(x, z);
            float erosion = biomeMap.getErosionAt(x, z);

            minTemp = std::min(minTemp, temp);
            maxTemp = std::max(maxTemp, temp);
            minMoisture = std::min(minMoisture, moisture);
            maxMoisture = std::max(maxMoisture, moisture);
            minWeirdness = std::min(minWeirdness, weirdness);
            maxWeirdness = std::max(maxWeirdness, weirdness);
            minErosion = std::min(minErosion, erosion);
            maxErosion = std::max(maxErosion, erosion);
        }
    }

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Temperature range: " << minTemp << " - " << maxTemp << " (expected: 0-100)\n";
    std::cout << "Moisture range: " << minMoisture << " - " << maxMoisture << " (expected: 0-100)\n";
    std::cout << "Weirdness range: " << minWeirdness << " - " << maxWeirdness << " (expected: 0-100)\n";
    std::cout << "Erosion range: " << minErosion << " - " << maxErosion << " (expected: 0-100)\n";

    // Validate ranges
    bool rangesValid =
        (minTemp >= 0.0f && maxTemp <= 100.0f) &&
        (minMoisture >= 0.0f && maxMoisture <= 100.0f) &&
        (minWeirdness >= 0.0f && maxWeirdness <= 100.0f) &&
        (minErosion >= 0.0f && maxErosion <= 100.0f);

    std::cout << "\nRange validation: " << (rangesValid ? "PASS" : "FAIL") << "\n";
}

void testBiomeContinuity() {
    std::cout << "\n=== Testing Biome Continuity ===\n";

    BiomeMap biomeMap(TEST_SEED);

    // Sample biomes along a straight line and count transitions
    int transitionCount = 0;
    const Biome* prevBiome = nullptr;

    for (int x = 0; x < TEST_AREA_SIZE; x += 5) {
        const Biome* biome = biomeMap.getBiomeAt(x, 500);

        if (prevBiome && biome != prevBiome) {
            transitionCount++;
            std::cout << "Transition at x=" << x << "\n";
        }

        prevBiome = biome;
    }

    std::cout << "\nTotal biome transitions across " << TEST_AREA_SIZE
              << " blocks: " << transitionCount << "\n";
    std::cout << "Average biome size: " << (transitionCount > 0 ? TEST_AREA_SIZE / transitionCount : TEST_AREA_SIZE)
              << " blocks\n";

    // With low frequency noise (0.0009-0.0013), we expect biomes to span hundreds of blocks
    bool largeScaleBiomes = (transitionCount < 10);  // Less than 10 transitions in 1000 blocks
    std::cout << "Large-scale biomes test: " << (largeScaleBiomes ? "PASS" : "FAIL") << "\n";
}

void testChunkSpanning() {
    std::cout << "\n=== Testing Chunk Spanning ===\n";

    constexpr int CHUNK_SIZE = 16;
    BiomeMap biomeMap(TEST_SEED);

    // Sample biome at each chunk corner in a 10x10 chunk area
    std::unordered_map<std::string, int> biomeChunkCount;

    for (int chunkX = 0; chunkX < 10; chunkX++) {
        for (int chunkZ = 0; chunkZ < 10; chunkZ++) {
            int worldX = chunkX * CHUNK_SIZE;
            int worldZ = chunkZ * CHUNK_SIZE;

            const Biome* biome = biomeMap.getBiomeAt(worldX, worldZ);
            if (biome) {
                biomeChunkCount[biome->name]++;
            }
        }
    }

    std::cout << "Biome distribution across 10x10 chunks (100 chunks):\n";
    for (const auto& pair : biomeChunkCount) {
        std::cout << "  " << pair.first << ": " << pair.second << " chunks\n";
    }

    // Each biome should span multiple chunks
    bool spansMultipleChunks = true;
    for (const auto& pair : biomeChunkCount) {
        if (pair.second < 2) {
            spansMultipleChunks = false;
            break;
        }
    }

    std::cout << "Chunk spanning test: " << (spansMultipleChunks ? "PASS" : "FAIL") << "\n";
}

void testNoiseVariety() {
    std::cout << "\n=== Testing Noise Variety (Multiple Layers) ===\n";

    BiomeMap biomeMap(TEST_SEED);

    // Sample two positions and verify they have different noise values
    // (unless extremely unlucky with RNG)

    float temp1 = biomeMap.getTemperatureAt(100, 100);
    float temp2 = biomeMap.getTemperatureAt(500, 500);

    float moisture1 = biomeMap.getMoistureAt(100, 100);
    float moisture2 = biomeMap.getMoistureAt(500, 500);

    float weirdness1 = biomeMap.getWeirdnessAt(100, 100);
    float weirdness2 = biomeMap.getWeirdnessAt(500, 500);

    float erosion1 = biomeMap.getErosionAt(100, 100);
    float erosion2 = biomeMap.getErosionAt(500, 500);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Position (100, 100): T=" << temp1 << ", M=" << moisture1
              << ", W=" << weirdness1 << ", E=" << erosion1 << "\n";
    std::cout << "Position (500, 500): T=" << temp2 << ", M=" << moisture2
              << ", W=" << weirdness2 << ", E=" << erosion2 << "\n";

    // Verify values are different (noise is working)
    bool hasVariety =
        (std::abs(temp1 - temp2) > 1.0f) ||
        (std::abs(moisture1 - moisture2) > 1.0f) ||
        (std::abs(weirdness1 - weirdness2) > 1.0f) ||
        (std::abs(erosion1 - erosion2) > 1.0f);

    std::cout << "Noise variety test: " << (hasVariety ? "PASS" : "FAIL") << "\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "  Biome Noise System Logic Tests\n";
    std::cout << "========================================\n";

    // Note: These tests validate the noise logic without requiring biomes to be loaded
    // In a real world, biomes would be loaded from YAML files

    testNoiseRanges();
    testBiomeContinuity();
    testChunkSpanning();
    testNoiseVariety();

    std::cout << "\n========================================\n";
    std::cout << "  Tests Complete\n";
    std::cout << "========================================\n";

    return 0;
}
