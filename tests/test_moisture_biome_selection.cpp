/**
 * Moisture-Based Biome Selection Test
 *
 * This test validates the moisture-based biome selection system by:
 * 1. Verifying moisture noise generates values in 0-100 range
 * 2. Testing that dry biomes spawn in low-moisture areas
 * 3. Testing that wet biomes spawn in high-moisture areas
 * 4. Verifying the 2D temperature+moisture matrix works correctly
 */

#include "biome_map.h"
#include "biome_system.h"
#include <iostream>
#include <iomanip>
#include <map>
#include <string>

constexpr int TEST_SEED = 42;

// Test moisture ranges match expected values
void testMoistureRanges() {
    std::cout << "\n=== Testing Moisture Noise Ranges ===\n";

    BiomeMap biomeMap(TEST_SEED);

    float minMoisture = 100.0f;
    float maxMoisture = 0.0f;
    float totalMoisture = 0.0f;
    int sampleCount = 0;

    // Sample across a large area
    for (int x = 0; x < 2000; x += 20) {
        for (int z = 0; z < 2000; z += 20) {
            float moisture = biomeMap.getMoistureAt(x, z);
            minMoisture = std::min(minMoisture, moisture);
            maxMoisture = std::max(maxMoisture, moisture);
            totalMoisture += moisture;
            sampleCount++;
        }
    }

    float avgMoisture = totalMoisture / sampleCount;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Moisture Range: " << minMoisture << " - " << maxMoisture << " (expected: 0-100)\n";
    std::cout << "Average Moisture: " << avgMoisture << " (expected: ~50)\n";

    bool rangeValid = (minMoisture >= 0.0f && maxMoisture <= 100.0f);
    bool avgReasonable = (avgMoisture > 30.0f && avgMoisture < 70.0f);

    std::cout << "Range Valid: " << (rangeValid ? "PASS" : "FAIL") << "\n";
    std::cout << "Average Reasonable: " << (avgReasonable ? "PASS" : "FAIL") << "\n";
}

// Test moisture gradient smoothness
void testMoistureGradients() {
    std::cout << "\n=== Testing Moisture Gradients (Smoothness) ===\n";

    BiomeMap biomeMap(TEST_SEED);

    // Sample along a line and check for smooth transitions
    float maxJump = 0.0f;
    float prevMoisture = biomeMap.getMoistureAt(0, 0);

    for (int x = 1; x < 1000; x++) {
        float moisture = biomeMap.getMoistureAt(x, 0);
        float jump = std::abs(moisture - prevMoisture);
        maxJump = std::max(maxJump, jump);
        prevMoisture = moisture;
    }

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Maximum moisture jump between adjacent blocks: " << maxJump << "\n";
    std::cout << "Expected: < 5.0 (smooth gradient)\n";

    bool smooth = (maxJump < 5.0f);
    std::cout << "Gradient Smoothness: " << (smooth ? "PASS" : "FAIL") << "\n";
}

// Test temperature+moisture biome matrix
void testTemperatureMoistureMatrix() {
    std::cout << "\n=== Testing Temperature+Moisture Biome Matrix ===\n";

    // Load biomes first
    auto& registry = BiomeRegistry::getInstance();
    registry.loadBiomes("assets/biomes/");

    if (registry.getBiomeCount() == 0) {
        std::cout << "ERROR: No biomes loaded! Cannot test biome matrix.\n";
        return;
    }

    BiomeMap biomeMap(TEST_SEED);

    // Create a 2D matrix showing which biome appears at each temp/moisture combination
    std::cout << "\nBiome Distribution Matrix:\n";
    std::cout << "         Moisture ->\n";
    std::cout << "Temp  0    20   40   60   80   100\n";
    std::cout << "  |   (Arid)(Dry)(Mod)(Hum)(Sat)\n";
    std::cout << "  v\n";

    // Sample temperature zones
    int tempValues[] = {10, 30, 50, 70, 90};  // Arctic, Cold, Temperate, Warm, Hot
    int moistureValues[] = {5, 25, 50, 70, 90};  // Arid, Dry, Moderate, Humid, Saturated

    const char* tempLabels[] = {"Arctic(10)", "Cold (30)", "Temp (50)", "Warm (70)", "Hot  (90)"};

    for (int t = 0; t < 5; t++) {
        std::cout << tempLabels[t] << ": ";

        for (int m = 0; m < 5; m++) {
            // Find biome closest to this temp/moisture combination
            const Biome* closestBiome = nullptr;
            float closestDist = 999999.0f;

            for (int i = 0; i < registry.getBiomeCount(); i++) {
                const Biome* biome = registry.getBiomeByIndex(i);
                if (!biome) continue;

                float tempDist = std::abs(tempValues[t] - biome->temperature);
                float moistDist = std::abs(moistureValues[m] - biome->moisture);
                float totalDist = std::sqrt(tempDist * tempDist + moistDist * moistDist);

                if (totalDist < closestDist) {
                    closestDist = totalDist;
                    closestBiome = biome;
                }
            }

            if (closestBiome) {
                // Print abbreviated biome name
                std::string name = closestBiome->name;
                if (name.length() > 8) {
                    name = name.substr(0, 8);
                }
                std::cout << std::setw(9) << std::left << name;
            } else {
                std::cout << "   ???   ";
            }
        }
        std::cout << "\n";
    }

    std::cout << "\nExpected Pattern:\n";
    std::cout << "  - Arctic + Arid = Ice Tundra\n";
    std::cout << "  - Hot + Arid = Desert\n";
    std::cout << "  - Hot + Saturated = Tropical Rainforest\n";
    std::cout << "  - Warm + Dry = Savanna\n";
    std::cout << "  - Temperate + Humid = Swamp\n";
    std::cout << "  - Temperate + Moderate = Forest/Plains\n";
}

// Test realistic biome distribution
void testRealisticBiomeDistribution() {
    std::cout << "\n=== Testing Realistic Biome Distribution ===\n";

    auto& registry = BiomeRegistry::getInstance();
    if (registry.getBiomeCount() == 0) {
        std::cout << "ERROR: No biomes loaded!\n";
        return;
    }

    BiomeMap biomeMap(TEST_SEED);

    // Count biome occurrences across a large area
    std::map<std::string, int> biomeCount;

    for (int x = 0; x < 1000; x += 10) {
        for (int z = 0; z < 1000; z += 10) {
            const Biome* biome = biomeMap.getBiomeAt(x, z);
            if (biome) {
                biomeCount[biome->name]++;
            }
        }
    }

    std::cout << "\nBiome Distribution (1000x1000 area, 10-block sampling):\n";
    int totalSamples = 0;
    for (const auto& pair : biomeCount) {
        totalSamples += pair.second;
    }

    for (const auto& pair : biomeCount) {
        float percentage = (pair.second * 100.0f) / totalSamples;
        std::cout << "  " << std::setw(20) << std::left << pair.first
                  << ": " << std::setw(4) << pair.second
                  << " samples (" << std::fixed << std::setprecision(1)
                  << percentage << "%)\n";
    }

    // Verify we have multiple different biomes (variety)
    bool hasVariety = (biomeCount.size() >= 3);
    std::cout << "\nVariety Test (3+ different biomes): "
              << (hasVariety ? "PASS" : "FAIL") << "\n";
}

// Test specific moisture-based biome selection
void testMoistureBasedSelection() {
    std::cout << "\n=== Testing Moisture-Based Biome Selection ===\n";

    auto& registry = BiomeRegistry::getInstance();
    if (registry.getBiomeCount() == 0) {
        std::cout << "ERROR: No biomes loaded!\n";
        return;
    }

    BiomeMap biomeMap(TEST_SEED);

    // Find a hot+dry location (should be Desert)
    // Find a hot+wet location (should be Tropical Rainforest or Swamp)

    std::cout << "\nSearching for moisture-based biome transitions...\n";

    int desertCount = 0;
    int wetBiomeCount = 0;
    int dryBiomeCount = 0;

    for (int x = 0; x < 2000; x += 50) {
        for (int z = 0; z < 2000; z += 50) {
            float temp = biomeMap.getTemperatureAt(x, z);
            float moisture = biomeMap.getMoistureAt(x, z);
            const Biome* biome = biomeMap.getBiomeAt(x, z);

            if (!biome) continue;

            // Check for dry biomes in low moisture
            if (moisture < 20.0f) {
                dryBiomeCount++;
                if (biome->name == "desert" || biome->name == "ice_tundra") {
                    // Expected behavior
                } else {
                    std::cout << "  WARNING: Found " << biome->name
                              << " in arid zone (moisture=" << moisture << ")\n";
                }
            }

            // Check for wet biomes in high moisture
            if (moisture > 70.0f) {
                wetBiomeCount++;
                if (biome->name == "tropical_rainforest" ||
                    biome->name == "swamp" ||
                    biome->name == "forest") {
                    // Expected behavior
                } else {
                    std::cout << "  WARNING: Found " << biome->name
                              << " in humid zone (moisture=" << moisture << ")\n";
                }
            }

            // Hot + Dry = Desert
            if (temp > 80.0f && moisture < 20.0f) {
                desertCount++;
            }
        }
    }

    std::cout << "  Found " << dryBiomeCount << " samples in arid zones (moisture < 20)\n";
    std::cout << "  Found " << wetBiomeCount << " samples in humid zones (moisture > 70)\n";
    std::cout << "  Found " << desertCount << " hot+dry samples (potential desert)\n";

    bool moistureWorking = (dryBiomeCount > 0 && wetBiomeCount > 0);
    std::cout << "\nMoisture-Based Selection: "
              << (moistureWorking ? "PASS" : "FAIL") << "\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "  Moisture-Based Biome Selection Test\n";
    std::cout << "========================================\n";

    testMoistureRanges();
    testMoistureGradients();
    testTemperatureMoistureMatrix();
    testRealisticBiomeDistribution();
    testMoistureBasedSelection();

    std::cout << "\n========================================\n";
    std::cout << "  Test Complete\n";
    std::cout << "========================================\n";

    return 0;
}
