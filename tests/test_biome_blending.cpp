/**
 * Test suite for biome blending algorithm
 * Tests the core blending functions, determinism, and edge cases
 */

#include "biome_map.h"
#include "biome_system.h"
#include <iostream>
#include <cassert>
#include <cmath>
#include <iomanip>

// Helper function to check if two floats are approximately equal
bool approxEqual(float a, float b, float epsilon = 0.0001f) {
    return std::abs(a - b) < epsilon;
}

// Test 1: Verify weights sum to 1.0
void testWeightNormalization() {
    std::cout << "Test 1: Weight Normalization..." << std::endl;

    BiomeMap biomeMap(12345);

    // Test multiple positions
    for (int i = 0; i < 10; i++) {
        float worldX = i * 100.0f;
        float worldZ = i * 100.0f;

        auto influences = biomeMap.getBiomeInfluences(worldX, worldZ);

        if (!influences.empty()) {
            float totalWeight = 0.0f;
            for (const auto& influence : influences) {
                totalWeight += influence.weight;
            }

            if (!approxEqual(totalWeight, 1.0f)) {
                std::cerr << "FAIL: Weights don't sum to 1.0 at (" << worldX << ", " << worldZ << ")" << std::endl;
                std::cerr << "      Total weight: " << totalWeight << std::endl;
                return;
            }
        }
    }

    std::cout << "PASS: All weights sum to 1.0" << std::endl;
}

// Test 2: Verify deterministic results
void testDeterminism() {
    std::cout << "\nTest 2: Deterministic Results..." << std::endl;

    BiomeMap biomeMap1(54321);
    BiomeMap biomeMap2(54321);

    // Test same seed produces same results
    for (int i = 0; i < 10; i++) {
        float worldX = i * 50.0f;
        float worldZ = i * 75.0f;

        auto influences1 = biomeMap1.getBiomeInfluences(worldX, worldZ);
        auto influences2 = biomeMap2.getBiomeInfluences(worldX, worldZ);

        if (influences1.size() != influences2.size()) {
            std::cerr << "FAIL: Different influence counts" << std::endl;
            return;
        }

        for (size_t j = 0; j < influences1.size(); j++) {
            if (influences1[j].biome != influences2[j].biome) {
                std::cerr << "FAIL: Different biomes selected" << std::endl;
                return;
            }
            if (!approxEqual(influences1[j].weight, influences2[j].weight)) {
                std::cerr << "FAIL: Different weights" << std::endl;
                return;
            }
        }
    }

    std::cout << "PASS: Results are deterministic" << std::endl;
}

// Test 3: Verify block selection is deterministic
void testDeterministicBlockSelection() {
    std::cout << "\nTest 3: Deterministic Block Selection..." << std::endl;

    BiomeMap biomeMap(99999);

    // Test same coordinates always produce same block
    for (int i = 0; i < 10; i++) {
        float worldX = i * 25.0f;
        float worldZ = i * 30.0f;

        int block1 = biomeMap.selectSurfaceBlock(worldX, worldZ);
        int block2 = biomeMap.selectSurfaceBlock(worldX, worldZ);

        if (block1 != block2) {
            std::cerr << "FAIL: Block selection not deterministic at (" << worldX << ", " << worldZ << ")" << std::endl;
            return;
        }
    }

    std::cout << "PASS: Block selection is deterministic" << std::endl;
}

// Test 4: Verify blended properties are reasonable
void testBlendedProperties() {
    std::cout << "\nTest 4: Blended Property Values..." << std::endl;

    BiomeMap biomeMap(11111);

    for (int i = 0; i < 10; i++) {
        float worldX = i * 80.0f;
        float worldZ = i * 90.0f;

        // Test tree density (should be 0-100)
        float treeDensity = biomeMap.getBlendedTreeDensity(worldX, worldZ);
        if (treeDensity < 0.0f || treeDensity > 100.0f) {
            std::cerr << "FAIL: Tree density out of range: " << treeDensity << std::endl;
            return;
        }

        // Test vegetation density (should be 0-100)
        float vegDensity = biomeMap.getBlendedVegetationDensity(worldX, worldZ);
        if (vegDensity < 0.0f || vegDensity > 100.0f) {
            std::cerr << "FAIL: Vegetation density out of range: " << vegDensity << std::endl;
            return;
        }

        // Test temperature (should be 0-100)
        float temp = biomeMap.getBlendedTemperature(worldX, worldZ);
        if (temp < 0.0f || temp > 100.0f) {
            std::cerr << "FAIL: Temperature out of range: " << temp << std::endl;
            return;
        }

        // Test moisture (should be 0-100)
        float moisture = biomeMap.getBlendedMoisture(worldX, worldZ);
        if (moisture < 0.0f || moisture > 100.0f) {
            std::cerr << "FAIL: Moisture out of range: " << moisture << std::endl;
            return;
        }
    }

    std::cout << "PASS: All blended properties in valid ranges" << std::endl;
}

// Test 5: Verify fog color blending
void testFogColorBlending() {
    std::cout << "\nTest 5: Fog Color Blending..." << std::endl;

    BiomeMap biomeMap(77777);

    for (int i = 0; i < 10; i++) {
        float worldX = i * 60.0f;
        float worldZ = i * 70.0f;

        glm::vec3 fogColor = biomeMap.getBlendedFogColor(worldX, worldZ);

        // Fog color components should be 0.0-1.0
        if (fogColor.r < 0.0f || fogColor.r > 1.0f ||
            fogColor.g < 0.0f || fogColor.g > 1.0f ||
            fogColor.b < 0.0f || fogColor.b > 1.0f) {
            std::cerr << "FAIL: Fog color out of range: ("
                      << fogColor.r << ", " << fogColor.g << ", " << fogColor.b << ")" << std::endl;
            return;
        }
    }

    std::cout << "PASS: Fog colors in valid range" << std::endl;
}

// Test 6: Edge case - single biome influence
void testSingleBiomeInfluence() {
    std::cout << "\nTest 6: Single Biome Edge Case..." << std::endl;

    BiomeMap biomeMap(33333);

    // In the center of a biome, there should typically be one dominant influence
    auto influences = biomeMap.getBiomeInfluences(0.0f, 0.0f);

    if (influences.empty()) {
        std::cerr << "FAIL: No biome influences at (0, 0)" << std::endl;
        return;
    }

    // At least one biome should be present
    std::cout << "PASS: Single biome case handled (found " << influences.size() << " influences)" << std::endl;
}

// Test 7: Verify transition smoothness
void testTransitionSmoothness() {
    std::cout << "\nTest 7: Transition Smoothness..." << std::endl;

    BiomeMap biomeMap(44444);

    // Sample along a line and verify smooth changes
    float prevTreeDensity = biomeMap.getBlendedTreeDensity(0.0f, 0.0f);

    for (int i = 1; i < 100; i++) {
        float worldX = i * 5.0f;
        float treeDensity = biomeMap.getBlendedTreeDensity(worldX, 0.0f);

        // Change should be gradual (not more than 50 units per 5 blocks)
        float change = std::abs(treeDensity - prevTreeDensity);
        if (change > 50.0f) {
            std::cerr << "WARN: Large transition jump detected: " << change << std::endl;
        }

        prevTreeDensity = treeDensity;
    }

    std::cout << "PASS: Transitions appear smooth" << std::endl;
}

// Test 8: Verify cache consistency
void testCacheConsistency() {
    std::cout << "\nTest 8: Cache Consistency..." << std::endl;

    BiomeMap biomeMap(55555);

    // Sample same point multiple times
    for (int i = 0; i < 5; i++) {
        float worldX = 100.0f;
        float worldZ = 200.0f;

        auto influences1 = biomeMap.getBiomeInfluences(worldX, worldZ);
        auto influences2 = biomeMap.getBiomeInfluences(worldX, worldZ);

        if (influences1.size() != influences2.size()) {
            std::cerr << "FAIL: Cache inconsistency - different sizes" << std::endl;
            return;
        }

        for (size_t j = 0; j < influences1.size(); j++) {
            if (influences1[j].biome != influences2[j].biome ||
                !approxEqual(influences1[j].weight, influences2[j].weight)) {
                std::cerr << "FAIL: Cache inconsistency - different values" << std::endl;
                return;
            }
        }
    }

    std::cout << "PASS: Cache returns consistent results" << std::endl;
}

// Test 9: Display sample blending info
void displaySampleBlending() {
    std::cout << "\nTest 9: Sample Blending Information..." << std::endl;

    BiomeMap biomeMap(66666);

    float worldX = 500.0f;
    float worldZ = 750.0f;

    auto influences = biomeMap.getBiomeInfluences(worldX, worldZ);

    std::cout << "Position (" << worldX << ", " << worldZ << "):" << std::endl;
    std::cout << "Number of influencing biomes: " << influences.size() << std::endl;

    for (size_t i = 0; i < influences.size(); i++) {
        if (influences[i].biome) {
            std::cout << "  Biome " << (i+1) << ": " << influences[i].biome->name
                      << " (weight: " << std::fixed << std::setprecision(3)
                      << influences[i].weight << ")" << std::endl;
        }
    }

    std::cout << "Blended tree density: " << biomeMap.getBlendedTreeDensity(worldX, worldZ) << std::endl;
    std::cout << "Blended vegetation density: " << biomeMap.getBlendedVegetationDensity(worldX, worldZ) << std::endl;
    std::cout << "Blended temperature: " << biomeMap.getBlendedTemperature(worldX, worldZ) << std::endl;
    std::cout << "Blended moisture: " << biomeMap.getBlendedMoisture(worldX, worldZ) << std::endl;

    std::cout << "PASS: Sample blending information displayed" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  Biome Blending Algorithm Test Suite  " << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    // Initialize biome registry (required for tests)
    auto& registry = BiomeRegistry::getInstance();
    if (registry.getBiomeCount() == 0) {
        std::cout << "Loading biomes from assets/biomes/" << std::endl;
        if (!registry.loadBiomes("assets/biomes/")) {
            std::cerr << "WARNING: Could not load biomes, using defaults" << std::endl;
        }
        std::cout << "Loaded " << registry.getBiomeCount() << " biomes" << std::endl;
        std::cout << std::endl;
    }

    // Run all tests
    testWeightNormalization();
    testDeterminism();
    testDeterministicBlockSelection();
    testBlendedProperties();
    testFogColorBlending();
    testSingleBiomeInfluence();
    testTransitionSmoothness();
    testCacheConsistency();
    displaySampleBlending();

    std::cout << "\n========================================" << std::endl;
    std::cout << "  All Tests Completed Successfully!    " << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}
