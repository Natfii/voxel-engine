/**
 * @file 3d_biome_usage_example.cpp
 * @brief Example demonstrating how to use the 3D biome influence system
 *
 * This file shows various use cases for the altitude-based biome modifications
 * including snow coverage, vertical transitions, and altitude-aware block selection.
 */

#include "biome_map.h"
#include "biome_system.h"
#include <iostream>
#include <vector>

/**
 * Example 1: Basic 3D biome influence query
 * Shows how to get altitude-modified biome influences at any 3D position
 */
void example1_basic_3d_influence(BiomeMap* biomeMap) {
    std::cout << "\n=== Example 1: Basic 3D Biome Influence ===\n";

    float worldX = 100.0f;
    float worldZ = 200.0f;

    // Compare biome influences at different altitudes
    std::vector<float> testAltitudes = {70.0f, 100.0f, 130.0f, 160.0f};

    for (float worldY : testAltitudes) {
        // Get 3D biome influences at this position
        auto influences = biomeMap->getBiomeInfluences3D(worldX, worldY, worldZ);

        std::cout << "\nAltitude Y=" << worldY << ":\n";
        std::cout << "  Influencing biomes: " << influences.size() << "\n";

        for (size_t i = 0; i < influences.size(); ++i) {
            const auto& inf = influences[i];
            std::cout << "  [" << i << "] " << inf.biome->name
                      << " - Weight: " << (inf.weight * 100.0f) << "%"
                      << " - Temp: " << inf.biome->temperature << "\n";
        }
    }
}

/**
 * Example 2: Snow coverage detection
 * Shows how to determine if snow should appear at different positions
 */
void example2_snow_coverage(BiomeMap* biomeMap) {
    std::cout << "\n=== Example 2: Snow Coverage Detection ===\n";

    // Test a mountain slope from base to peak
    float worldX = 500.0f;
    float worldZ = 500.0f;

    std::cout << "\nMountain slope analysis:\n";
    std::cout << "X=" << worldX << ", Z=" << worldZ << "\n\n";

    // Check snow coverage at different altitudes
    for (int y = 60; y <= 150; y += 10) {
        float worldY = static_cast<float>(y);

        bool hasSnow = biomeMap->shouldApplySnowCover(worldX, worldY, worldZ);
        float tempDrop = biomeMap->getAltitudeTemperatureModifier(worldY);

        std::cout << "Y=" << y
                  << " - Snow: " << (hasSnow ? "YES" : "NO ")
                  << " - Temp drop: -" << tempDrop << "°\n";
    }
}

/**
 * Example 3: Altitude-modified block selection
 * Demonstrates how surface blocks change with altitude
 */
void example3_altitude_blocks(BiomeMap* biomeMap) {
    std::cout << "\n=== Example 3: Altitude-Modified Blocks ===\n";

    float worldX = 1000.0f;
    float worldZ = 1000.0f;

    // Get the base terrain height
    int terrainHeight = biomeMap->getTerrainHeightAt(worldX, worldZ);
    std::cout << "\nTerrain height: " << terrainHeight << "\n";

    // Get base 2D biome
    const Biome* baseBiome = biomeMap->getBiomeAt(worldX, worldZ);
    int baseSurfaceBlock = baseBiome->primary_surface_block;

    std::cout << "Base biome: " << baseBiome->name
              << " (surface block: " << baseSurfaceBlock << ")\n\n";

    // Show how blocks change with altitude
    std::cout << "Altitude transitions:\n";
    for (int offset = 0; offset <= 30; offset += 3) {
        float worldY = static_cast<float>(terrainHeight + offset);

        int modifiedBlock = biomeMap->getAltitudeModifiedBlock(
            worldX, worldY, worldZ, baseSurfaceBlock
        );

        const char* blockName = "Unknown";
        if (modifiedBlock == 1) blockName = "Stone";
        else if (modifiedBlock == 3) blockName = "Grass";
        else if (modifiedBlock == 4) blockName = "Dirt";
        else if (modifiedBlock == 7) blockName = "Sand";
        else if (modifiedBlock == 8) blockName = "Snow";

        std::cout << "  +" << offset << " blocks above terrain (Y=" << worldY << "): "
                  << blockName << " (ID: " << modifiedBlock << ")\n";
    }
}

/**
 * Example 4: Temperature gradient analysis
 * Shows how temperature changes with altitude
 */
void example4_temperature_gradient(BiomeMap* biomeMap) {
    std::cout << "\n=== Example 4: Temperature Gradient ===\n";

    float worldX = 750.0f;
    float worldZ = 750.0f;

    // Get base temperature at this XZ position
    float baseTemp = biomeMap->getTemperatureAt(worldX, worldZ);

    std::cout << "\nBase temperature (sea level): " << baseTemp << "°\n";
    std::cout << "\nAltitude temperature profile:\n";

    // Show temperature at different altitudes
    for (int y = 50; y <= 180; y += 10) {
        float tempModifier = biomeMap->getAltitudeTemperatureModifier(static_cast<float>(y));
        float effectiveTemp = baseTemp - tempModifier;

        std::cout << "Y=" << y
                  << " - Temperature: " << effectiveTemp << "°"
                  << " (drop: -" << tempModifier << "°)\n";
    }
}

/**
 * Example 5: Vertical biome transition analysis
 * Shows how biome composition changes with altitude in a mixed biome area
 */
void example5_vertical_transition(BiomeMap* biomeMap) {
    std::cout << "\n=== Example 5: Vertical Biome Transition ===\n";

    // Choose a position at a biome boundary for interesting results
    float worldX = 1500.0f;
    float worldZ = 1500.0f;

    int terrainHeight = biomeMap->getTerrainHeightAt(worldX, worldZ);

    std::cout << "\nPosition: X=" << worldX << ", Z=" << worldZ << "\n";
    std::cout << "Terrain height: " << terrainHeight << "\n\n";

    // Analyze biome composition at different heights
    for (int offset = 0; offset <= 40; offset += 10) {
        float worldY = static_cast<float>(terrainHeight + offset);

        auto influences = biomeMap->getBiomeInfluences3D(worldX, worldY, worldZ);

        std::cout << "Altitude +" << offset << " (Y=" << worldY << "):\n";

        if (!influences.empty()) {
            // Show dominant biome
            std::cout << "  Dominant: " << influences[0].biome->name
                      << " (" << (influences[0].weight * 100.0f) << "%)\n";

            // Show all influencing biomes
            if (influences.size() > 1) {
                std::cout << "  Others: ";
                for (size_t i = 1; i < influences.size(); ++i) {
                    std::cout << influences[i].biome->name << " ("
                              << (influences[i].weight * 100.0f) << "%)";
                    if (i < influences.size() - 1) std::cout << ", ";
                }
                std::cout << "\n";
            }
        }

        std::cout << "\n";
    }
}

/**
 * Example 6: Altitude influence factor visualization
 * Shows the altitude influence curve
 */
void example6_altitude_influence_curve(BiomeMap* biomeMap) {
    std::cout << "\n=== Example 6: Altitude Influence Curve ===\n";

    float worldX = 2000.0f;
    float worldZ = 2000.0f;

    int terrainHeight = biomeMap->getTerrainHeightAt(worldX, worldZ);

    std::cout << "\nTerrain height: " << terrainHeight << "\n";
    std::cout << "\nAltitude influence factor (0.0 = no effect, 1.0 = full effect):\n\n";

    // Show influence curve from below terrain to high above
    for (int offset = -10; offset <= 30; offset += 2) {
        float worldY = static_cast<float>(terrainHeight + offset);
        float influence = biomeMap->getAltitudeInfluence(worldY, terrainHeight);

        // Create a simple bar chart
        std::cout << "  ";
        if (offset < 0) std::cout << offset;
        else std::cout << "+" << offset;

        std::cout << " blocks: ";

        // Draw bar
        int barLength = static_cast<int>(influence * 40.0f);
        for (int i = 0; i < barLength; ++i) {
            std::cout << "█";
        }

        std::cout << " " << (influence * 100.0f) << "%\n";
    }
}

/**
 * Example 7: Practical chunk generation usage
 * Shows how to integrate 3D biome system into chunk generation
 */
void example7_chunk_generation_usage(BiomeMap* biomeMap) {
    std::cout << "\n=== Example 7: Chunk Generation Integration ===\n";

    // Simulate chunk generation at chunk coordinates (5, 3, 7)
    int chunkX = 5, chunkY = 3, chunkZ = 7;
    const int CHUNK_SIZE = 32;

    std::cout << "\nGenerating chunk (" << chunkX << ", " << chunkY << ", " << chunkZ << ")\n";

    // Sample a few positions in the chunk
    std::cout << "\nSample block generation:\n";

    for (int sampleIdx = 0; sampleIdx < 3; ++sampleIdx) {
        // Random sample positions in chunk
        int localX = sampleIdx * 10;
        int localY = sampleIdx * 10;
        int localZ = sampleIdx * 10;

        // Convert to world coordinates
        float worldX = static_cast<float>(chunkX * CHUNK_SIZE + localX);
        float worldY = static_cast<float>(chunkY * CHUNK_SIZE + localY);
        float worldZ = static_cast<float>(chunkZ * CHUNK_SIZE + localZ);

        // Get terrain height
        int terrainHeight = biomeMap->getTerrainHeightAt(worldX, worldZ);

        std::cout << "\n  Position [" << localX << ", " << localY << ", " << localZ << "] "
                  << "(World: " << worldX << ", " << worldY << ", " << worldZ << ")\n";

        // Determine block type based on altitude
        if (worldY < terrainHeight) {
            std::cout << "    Below terrain - Generate solid blocks\n";

            // Get 3D influences for underground
            auto influences = biomeMap->getBiomeInfluences3D(worldX, worldY, worldZ);
            if (!influences.empty()) {
                std::cout << "    Dominant biome: " << influences[0].biome->name << "\n";
                std::cout << "    Stone type: " << influences[0].biome->primary_stone_block << "\n";
            }
        } else if (worldY == terrainHeight) {
            std::cout << "    At surface - Generate surface block\n";

            // Get base surface block
            const Biome* biome = biomeMap->getBiomeAt(worldX, worldZ);
            int baseBlock = biome->primary_surface_block;

            // Apply altitude modifications
            int finalBlock = biomeMap->getAltitudeModifiedBlock(worldX, worldY, worldZ, baseBlock);

            std::cout << "    Base block: " << baseBlock << "\n";
            std::cout << "    Altitude-modified block: " << finalBlock << "\n";

            if (biomeMap->shouldApplySnowCover(worldX, worldY, worldZ)) {
                std::cout << "    ❄ Snow coverage applied\n";
            }
        } else {
            std::cout << "    Above terrain - Air or water\n";
        }
    }
}

/**
 * Main function - runs all examples
 */
int main() {
    std::cout << "====================================\n";
    std::cout << "3D Biome Influence System - Examples\n";
    std::cout << "====================================\n";

    // Initialize biome system
    BiomeRegistry& registry = BiomeRegistry::getInstance();

    // In a real application, load biomes from files:
    // registry.loadBiomes("assets/biomes/");

    // For this example, we assume biomes are loaded
    if (registry.getBiomeCount() == 0) {
        std::cerr << "\nERROR: No biomes loaded!\n";
        std::cerr << "Please load biomes before running examples.\n";
        return 1;
    }

    // Create biome map with seed
    int seed = 12345;
    BiomeMap biomeMap(seed);

    std::cout << "\nBiome system initialized with seed: " << seed << "\n";
    std::cout << "Loaded biomes: " << registry.getBiomeCount() << "\n";

    // Run examples
    example1_basic_3d_influence(&biomeMap);
    example2_snow_coverage(&biomeMap);
    example3_altitude_blocks(&biomeMap);
    example4_temperature_gradient(&biomeMap);
    example5_vertical_transition(&biomeMap);
    example6_altitude_influence_curve(&biomeMap);
    example7_chunk_generation_usage(&biomeMap);

    std::cout << "\n====================================\n";
    std::cout << "All examples completed!\n";
    std::cout << "====================================\n";

    return 0;
}
