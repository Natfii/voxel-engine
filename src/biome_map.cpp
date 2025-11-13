#include "biome_map.h"
#include "terrain_constants.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

BiomeMap::BiomeMap(int seed) {
    // Temperature noise - VERY large scale, creates modern Minecraft-style massive climate zones
    // Lower frequency = wider biomes (0.001 = ~1000 block features, modern Minecraft 1.18+ scale)
    m_temperatureNoise = std::make_unique<FastNoiseLite>(seed);
    m_temperatureNoise->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_temperatureNoise->SetFractalType(FastNoiseLite::FractalType_FBm);
    m_temperatureNoise->SetFractalOctaves(4);
    m_temperatureNoise->SetFrequency(0.001f);  // Modern Minecraft scale (1.18+ biomes: 800-1500 blocks)

    // Temperature variation - adds local temperature changes within biomes
    m_temperatureVariation = std::make_unique<FastNoiseLite>(seed + 1000);
    m_temperatureVariation->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_temperatureVariation->SetFrequency(0.012f);  // Medium features for variation

    // Moisture noise - VERY large scale, creates modern Minecraft-style massive wet/dry zones
    m_moistureNoise = std::make_unique<FastNoiseLite>(seed + 100);
    m_moistureNoise->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_moistureNoise->SetFractalType(FastNoiseLite::FractalType_FBm);
    m_moistureNoise->SetFractalOctaves(4);
    m_moistureNoise->SetFrequency(0.0012f);  // Modern Minecraft scale (1.18+ biomes: 800-1500 blocks)

    // Moisture variation - adds local moisture changes within biomes
    m_moistureVariation = std::make_unique<FastNoiseLite>(seed + 1100);
    m_moistureVariation->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_moistureVariation->SetFrequency(0.015f);  // Medium features for variation

    // Terrain height noise - controlled by biome age
    m_terrainNoise = std::make_unique<FastNoiseLite>(seed + 200);
    m_terrainNoise->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_terrainNoise->SetFractalType(FastNoiseLite::FractalType_FBm);
    m_terrainNoise->SetFractalOctaves(5);
    m_terrainNoise->SetFractalLacunarity(2.0f);
    m_terrainNoise->SetFractalGain(0.5f);
    m_terrainNoise->SetFrequency(0.015f);

    // Cave noise - 3D worley noise for natural cave systems
    m_caveNoise = std::make_unique<FastNoiseLite>(seed + 300);
    m_caveNoise->SetNoiseType(FastNoiseLite::NoiseType_Cellular);
    m_caveNoise->SetCellularDistanceFunction(FastNoiseLite::CellularDistanceFunction_Euclidean);
    m_caveNoise->SetCellularReturnType(FastNoiseLite::CellularReturnType_Distance);
    m_caveNoise->SetFrequency(0.03f);  // Frequency controls cave size

    // Underground chamber noise - creates contained underground biome pockets
    // Higher frequency than surface biomes = smaller, more contained underground areas
    m_undergroundChamberNoise = std::make_unique<FastNoiseLite>(seed + 400);
    m_undergroundChamberNoise->SetNoiseType(FastNoiseLite::NoiseType_Cellular);
    m_undergroundChamberNoise->SetCellularDistanceFunction(FastNoiseLite::CellularDistanceFunction_Euclidean);
    m_undergroundChamberNoise->SetCellularReturnType(FastNoiseLite::CellularReturnType_Distance2);
    m_undergroundChamberNoise->SetFrequency(0.006f);  // Contained chambers (3x smaller than surface biomes)
}

float BiomeMap::getTemperatureAt(float worldX, float worldZ) {
    // FastNoiseLite is thread-safe for reads - no mutex needed
    // This was causing catastrophic mutex contention during parallel chunk generation

    // Base temperature from large-scale noise
    float baseTemp = m_temperatureNoise->GetNoise(worldX, worldZ);

    // Add variation for more interesting temperature patterns
    float variation = m_temperatureVariation->GetNoise(worldX, worldZ);

    // Combine: 70% base + 30% variation
    float combined = (baseTemp * 0.7f) + (variation * 0.3f);

    // Map from [-1, 1] to [0, 100]
    return mapNoiseToRange(combined, 0.0f, 100.0f);
}

float BiomeMap::getMoistureAt(float worldX, float worldZ) {
    // FastNoiseLite is thread-safe for reads - no mutex needed

    // Base moisture from large-scale noise
    float baseMoisture = m_moistureNoise->GetNoise(worldX, worldZ);

    // Add variation
    float variation = m_moistureVariation->GetNoise(worldX, worldZ);

    // Combine: 70% base + 30% variation
    float combined = (baseMoisture * 0.7f) + (variation * 0.3f);

    // Map from [-1, 1] to [0, 100]
    return mapNoiseToRange(combined, 0.0f, 100.0f);
}

const Biome* BiomeMap::getBiomeAt(float worldX, float worldZ) {
    // Create a cache key (quantize to reduce cache size)
    // We cache at 4-block resolution - close enough for smooth transitions
    int quantizedX = static_cast<int>(worldX / 4.0f);
    int quantizedZ = static_cast<int>(worldZ / 4.0f);
    uint64_t key = coordsToKey(quantizedX, quantizedZ);

    // Check cache first
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        auto it = m_biomeCache.find(key);
        if (it != m_biomeCache.end()) {
            return it->second.biome;
        }
    }

    // Not in cache - compute it
    float temperature = getTemperatureAt(worldX, worldZ);
    float moisture = getMoistureAt(worldX, worldZ);
    const Biome* biome = selectBiome(temperature, moisture);

    // Cache it (with size limit to prevent memory leak)
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        // If cache is too large, clear it (simple eviction strategy)
        if (m_biomeCache.size() >= MAX_CACHE_SIZE) {
            m_biomeCache.clear();  // Clear entire cache when limit reached
        }
        m_biomeCache[key] = {biome, temperature, moisture};
    }

    return biome;
}

int BiomeMap::getTerrainHeightAt(float worldX, float worldZ) {
    using namespace TerrainGeneration;

    // Get biome at this location
    const Biome* biome = getBiomeAt(worldX, worldZ);
    if (!biome) {
        return BASE_HEIGHT;
    }

    // FastNoiseLite is thread-safe for reads - no mutex needed
    float noise = m_terrainNoise->GetNoise(worldX, worldZ);

    // Use biome's age to control terrain roughness
    // Age 0 (young) = very rough, mountainous (high variation)
    // Age 100 (old) = very flat, plains-like (low variation)

    // Calculate height variation based on age
    // Young terrain (age=0): variation = 30 blocks
    // Old terrain (age=100): variation = 5 blocks
    float ageNormalized = biome->age / 100.0f;  // 0.0 to 1.0
    float heightVariation = 30.0f - (ageNormalized * 25.0f);  // 30 to 5

    // Apply biome's height multiplier for special terrain (mountains, etc.)
    heightVariation *= biome->height_multiplier;

    // Calculate final height
    int height = BASE_HEIGHT + static_cast<int>(noise * heightVariation);

    // Apply biome's lowest_y constraint
    if (height < biome->lowest_y) {
        height = biome->lowest_y;
    }

    return height;
}

float BiomeMap::getCaveDensityAt(float worldX, float worldY, float worldZ) {
    // FastNoiseLite is thread-safe for reads - no mutex needed

    // Get 3D cave noise
    float caveNoise = m_caveNoise->GetNoise(worldX, worldY * 0.5f, worldZ);

    // Map from [-1, 1] to [0, 1]
    float density = mapNoiseTo01(caveNoise);

    // Make caves rarer near the surface (top 20 blocks)
    if (worldY > 50) {
        float surfaceProximity = std::min(1.0f, (worldY - 50.0f) / 20.0f);
        density = density + surfaceProximity * (1.0f - density);  // Push towards solid
    }

    // Cave threshold: < 0.45 = air (cave), >= 0.45 = solid
    // We return density where higher = more solid
    return density;
}

bool BiomeMap::isUndergroundBiomeAt(float worldX, float worldY, float worldZ) {
    // Underground chambers restricted to mid-depth range to preserve mining gameplay
    // Only generate between Y=10 and Y=45 (prevents Swiss-cheese deep underground)
    if (worldY < 10.0f || worldY > 45.0f) {
        return false;
    }

    // FastNoiseLite is thread-safe for reads - no mutex needed

    // Sample chamber noise
    float chamberNoise = m_undergroundChamberNoise->GetNoise(worldX, worldY * 0.3f, worldZ);

    // Map to [0, 1]
    float value = mapNoiseTo01(chamberNoise);

    // Large chambers where value > 0.6 (creates separated biome pockets)
    return value > 0.6f;
}

// ==================== Private Helper Functions ====================

uint64_t BiomeMap::coordsToKey(int x, int z) const {
    // Combine x and z into a single 64-bit key
    // Handle negative coordinates correctly by preserving bit patterns
    uint32_t ux;
    uint32_t uz;
    std::memcpy(&ux, &x, sizeof(uint32_t));
    std::memcpy(&uz, &z, sizeof(uint32_t));
    return (static_cast<uint64_t>(ux) << 32) | static_cast<uint64_t>(uz);
}

const Biome* BiomeMap::selectBiome(float temperature, float moisture) {
    auto& biomeRegistry = BiomeRegistry::getInstance();

    if (biomeRegistry.getBiomeCount() == 0) {
        std::cerr << "No biomes loaded!" << std::endl;
        return nullptr;
    }

    // Find all biomes that match this temperature and moisture
    // We'll use a tolerance range to allow biomes to blend at boundaries
    const float TOLERANCE = 15.0f;

    std::vector<const Biome*> candidates;
    std::vector<float> weights;

    for (int i = 0; i < biomeRegistry.getBiomeCount(); i++) {
        const Biome* biome = biomeRegistry.getBiomeByIndex(i);
        if (!biome) continue;

        // Calculate distance from biome's preferred temp/moisture
        float tempDist = std::abs(temperature - biome->temperature);
        float moistureDist = std::abs(moisture - biome->moisture);

        // If within tolerance, consider this biome
        if (tempDist <= TOLERANCE && moistureDist <= TOLERANCE) {
            // Weight by proximity (closer = higher weight) and rarity
            float proximityWeight = 1.0f - (tempDist + moistureDist) / (TOLERANCE * 2.0f);
            float rarityWeight = biome->biome_rarity_weight / 50.0f;  // Normalize around 1.0

            float totalWeight = proximityWeight * rarityWeight;
            candidates.push_back(biome);
            weights.push_back(totalWeight);
        }
    }

    // If no candidates found, find the closest biome
    if (candidates.empty()) {
        const Biome* closestBiome = nullptr;
        float closestDist = 999999.0f;

        for (int i = 0; i < biomeRegistry.getBiomeCount(); i++) {
            const Biome* biome = biomeRegistry.getBiomeByIndex(i);
            if (!biome) continue;

            float tempDist = std::abs(temperature - biome->temperature);
            float moistureDist = std::abs(moisture - biome->moisture);
            float totalDist = tempDist + moistureDist;

            if (totalDist < closestDist) {
                closestDist = totalDist;
                closestBiome = biome;
            }
        }

        return closestBiome;
    }

    // Select the candidate with the highest weight
    // This creates smooth biome transitions
    size_t bestIndex = 0;
    float bestWeight = weights[0];

    for (size_t i = 1; i < weights.size(); i++) {
        if (weights[i] > bestWeight) {
            bestWeight = weights[i];
            bestIndex = i;
        }
    }

    return candidates[bestIndex];
}

float BiomeMap::mapNoiseTo01(float noise) {
    // Map from [-1, 1] to [0, 1]
    return (noise + 1.0f) * 0.5f;
}

float BiomeMap::mapNoiseToRange(float noise, float min, float max) {
    // Map from [-1, 1] to [min, max]
    float normalized = (noise + 1.0f) * 0.5f;  // [0, 1]
    return min + (normalized * (max - min));
}
