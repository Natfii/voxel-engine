// Quick test to verify biome scale increase
// Tests that biomes now span wider areas (4-8+ chunks instead of 1-2)

#include <iostream>
#include <set>
#include "include/biome_map.h"
#include "include/biome_system.h"

int main() {
    std::cout << "=== BIOME SCALE TEST ===" << std::endl;
    std::cout << "Testing that biomes span 4-8+ chunks (64-128+ blocks)" << std::endl;
    std::cout << std::endl;

    // Initialize biome system
    auto& biomeRegistry = BiomeRegistry::getInstance();
    biomeRegistry.loadBiomes("assets/biomes/");

    // Create BiomeMap with test seed
    int testSeed = 12345;
    BiomeMap biomeMap(testSeed);

    // Test 1: Check biome consistency across 64 blocks (4 chunks)
    std::cout << "Test 1: Checking biome consistency across 64 blocks (4 chunks)..." << std::endl;
    const Biome* startBiome = biomeMap.getBiomeAt(0, 0);
    int sameCount = 0;
    for (int x = 0; x < 64; x++) {
        const Biome* biome = biomeMap.getBiomeAt(x, 0);
        if (biome == startBiome) {
            sameCount++;
        }
    }
    float consistency4Chunks = (float)sameCount / 64.0f * 100.0f;
    std::cout << "  Same biome across 4 chunks: " << consistency4Chunks << "%" << std::endl;

    // Test 2: Check biome consistency across 128 blocks (8 chunks)
    std::cout << "Test 2: Checking biome consistency across 128 blocks (8 chunks)..." << std::endl;
    sameCount = 0;
    for (int x = 0; x < 128; x++) {
        const Biome* biome = biomeMap.getBiomeAt(x, 0);
        if (biome == startBiome) {
            sameCount++;
        }
    }
    float consistency8Chunks = (float)sameCount / 128.0f * 100.0f;
    std::cout << "  Same biome across 8 chunks: " << consistency8Chunks << "%" << std::endl;

    // Test 3: Count unique biomes in a 256x256 area (16x16 chunks)
    std::cout << "Test 3: Counting unique biomes in 256x256 block area..." << std::endl;
    std::set<const Biome*> uniqueBiomes;
    for (int z = 0; z < 256; z += 16) {
        for (int x = 0; x < 256; x += 16) {
            uniqueBiomes.insert(biomeMap.getBiomeAt(x, z));
        }
    }
    std::cout << "  Unique biomes found: " << uniqueBiomes.size() << std::endl;

    // Test 4: Measure average biome size by sampling
    std::cout << "Test 4: Estimating average biome width..." << std::endl;
    int transitionCount = 0;
    const Biome* prevBiome = biomeMap.getBiomeAt(0, 100);
    for (int x = 1; x < 1000; x++) {
        const Biome* currentBiome = biomeMap.getBiomeAt(x, 100);
        if (currentBiome != prevBiome) {
            transitionCount++;
            prevBiome = currentBiome;
        }
    }
    float avgBiomeWidth = 1000.0f / (transitionCount + 1);
    float avgChunks = avgBiomeWidth / 16.0f;
    std::cout << "  Average biome width: ~" << avgBiomeWidth << " blocks (~"
              << avgChunks << " chunks)" << std::endl;

    // Results
    std::cout << std::endl << "=== RESULTS ===" << std::endl;
    if (consistency4Chunks > 60.0f && avgChunks >= 4.0f) {
        std::cout << "SUCCESS: Biomes now span 4-8+ chunks!" << std::endl;
        std::cout << "  - 4-chunk consistency: " << consistency4Chunks << "% (target: >60%)" << std::endl;
        std::cout << "  - Average biome size: " << avgChunks << " chunks (target: 4-8+)" << std::endl;
    } else {
        std::cout << "NEEDS TUNING: Biomes may not be wide enough yet" << std::endl;
        std::cout << "  - 4-chunk consistency: " << consistency4Chunks << "% (target: >60%)" << std::endl;
        std::cout << "  - Average biome size: " << avgChunks << " chunks (target: 4-8+)" << std::endl;
    }

    return 0;
}
