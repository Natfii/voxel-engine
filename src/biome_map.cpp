#include "biome_map.h"
#include "terrain_constants.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

BiomeMap::BiomeMap(int seed) {
    // Temperature noise - EXTRA large scale for expansive biomes
    // Lower frequency = wider biomes (0.0004 = ~2500 block features, 2.5x larger than before)
    m_temperatureNoise = std::make_unique<FastNoiseLite>(seed);
    m_temperatureNoise->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_temperatureNoise->SetFractalType(FastNoiseLite::FractalType_FBm);
    m_temperatureNoise->SetFractalOctaves(4);
    m_temperatureNoise->SetFrequency(0.0004f);  // Expansive biomes: 2000-3000 blocks wide

    // Temperature variation - adds local temperature changes within biomes
    m_temperatureVariation = std::make_unique<FastNoiseLite>(seed + 1000);
    m_temperatureVariation->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_temperatureVariation->SetFrequency(0.008f);  // Smoother variation for natural blending

    // Moisture noise - EXTRA large scale for expansive wet/dry zones
    m_moistureNoise = std::make_unique<FastNoiseLite>(seed + 100);
    m_moistureNoise->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_moistureNoise->SetFractalType(FastNoiseLite::FractalType_FBm);
    m_moistureNoise->SetFractalOctaves(4);
    m_moistureNoise->SetFrequency(0.0005f);  // Expansive biomes: 2000-3000 blocks wide

    // Moisture variation - adds local moisture changes within biomes
    m_moistureVariation = std::make_unique<FastNoiseLite>(seed + 1100);
    m_moistureVariation->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_moistureVariation->SetFrequency(0.010f);  // Smoother variation for natural blending

    // Terrain height noise - controlled by biome age
    m_terrainNoise = std::make_unique<FastNoiseLite>(seed + 200);
    m_terrainNoise->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_terrainNoise->SetFractalType(FastNoiseLite::FractalType_FBm);
    m_terrainNoise->SetFractalOctaves(5);
    m_terrainNoise->SetFractalLacunarity(2.0f);
    m_terrainNoise->SetFractalGain(0.5f);
    m_terrainNoise->SetFrequency(0.015f);

    // Cave noise - 3D cellular noise for chamber-style caves
    m_caveNoise = std::make_unique<FastNoiseLite>(seed + 300);
    m_caveNoise->SetNoiseType(FastNoiseLite::NoiseType_Cellular);
    m_caveNoise->SetCellularDistanceFunction(FastNoiseLite::CellularDistanceFunction_Euclidean);
    m_caveNoise->SetCellularReturnType(FastNoiseLite::CellularReturnType_Distance);
    m_caveNoise->SetFrequency(0.03f);  // Frequency controls cave size

    // Cave tunnel noise - creates long winding tunnels to connect underground biomes
    // Uses OpenSimplex2 with FBm for natural winding patterns
    m_caveTunnelNoise = std::make_unique<FastNoiseLite>(seed + 350);
    m_caveTunnelNoise->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_caveTunnelNoise->SetFractalType(FastNoiseLite::FractalType_FBm);
    m_caveTunnelNoise->SetFractalOctaves(3);  // More octaves = more winding detail
    m_caveTunnelNoise->SetFrequency(0.02f);   // Lower frequency = longer, wider tunnels

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

    // Add distance-based temperature gradient for variety as you travel far
    // Distance from world origin (0, 0)
    float distanceFromOrigin = std::sqrt(worldX * worldX + worldZ * worldZ);

    // Create gradual temperature shift based on distance (every ~5000 blocks = full temp range)
    // This creates natural temperature zones as you explore further from spawn
    float distanceInfluence = std::sin(distanceFromOrigin * 0.0004f) * 0.3f;  // ±30% temp shift

    // Apply distance influence to create exploration variety
    combined = combined + distanceInfluence;

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

    // Check cache first (shared lock - multiple threads can read simultaneously)
    {
        std::shared_lock<std::shared_mutex> lock(m_cacheMutex);
        auto it = m_biomeCache.find(key);
        if (it != m_biomeCache.end()) {
            return it->second.biome;
        }
    }

    // Not in cache - compute it
    float temperature = getTemperatureAt(worldX, worldZ);
    float moisture = getMoistureAt(worldX, worldZ);
    const Biome* biome = selectBiome(temperature, moisture);

    // Cache it (exclusive lock for write)
    {
        std::unique_lock<std::shared_mutex> lock(m_cacheMutex);

        // Double-check: another thread may have cached it while we computed
        auto it = m_biomeCache.find(key);
        if (it != m_biomeCache.end()) {
            return it->second.biome;  // Use existing cached value
        }

        // LRU-style eviction: remove oldest 20% when cache is full (better than clearing all)
        if (m_biomeCache.size() >= MAX_CACHE_SIZE) {
            size_t removeCount = MAX_CACHE_SIZE / 5;  // Remove 20% (~20K entries)
            auto removeIt = m_biomeCache.begin();
            for (size_t i = 0; i < removeCount && removeIt != m_biomeCache.end(); ++i) {
                removeIt = m_biomeCache.erase(removeIt);
            }
        }

        m_biomeCache[key] = {biome, temperature, moisture};
    }

    return biome;
}

int BiomeMap::getTerrainHeightAt(float worldX, float worldZ) {
    using namespace TerrainGeneration;

    // WEEK 1 OPTIMIZATION: Cache terrain height (quantize to 2-block resolution)
    int quantizedX = static_cast<int>(worldX / 2.0f);
    int quantizedZ = static_cast<int>(worldZ / 2.0f);
    uint64_t key = coordsToKey(quantizedX, quantizedZ);

    // Check cache first
    {
        std::shared_lock<std::shared_mutex> lock(m_terrainCacheMutex);
        auto it = m_terrainHeightCache.find(key);
        if (it != m_terrainHeightCache.end()) {
            return it->second;
        }
    }

    // Not in cache - compute it
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

    // Cache result (with size limit)
    {
        std::unique_lock<std::shared_mutex> lock(m_terrainCacheMutex);
        if (m_terrainHeightCache.size() < MAX_CACHE_SIZE) {
            m_terrainHeightCache[key] = height;
        }
    }

    // NOTE: Do NOT clamp to lowest_y here - that creates floating terrain with void underneath!
    // The lowest_y property is for biome spawn elevation, not terrain height limits.
    // We must always generate terrain from Y=0 upward to avoid gaps.

    return height;
}

float BiomeMap::getCaveDensityAt(float worldX, float worldY, float worldZ) {
    // FastNoiseLite is thread-safe for reads - no mutex needed
    // Note: Cave density caching disabled - noise lookups are fast enough

    // === Winding Tunnel System (primary cave network) ===
    // Creates long, continuous tunnels that connect underground biomes
    // Technique: Sample 3D noise and create tunnels where abs(noise) is close to a target value
    float tunnelNoise = m_caveTunnelNoise->GetNoise(worldX, worldY, worldZ);

    // Create winding tunnels where noise is close to 0 (creates tubular structure)
    // Tunnel forms when |noise| < threshold (creates continuous winding paths)
    float tunnelRadius = 0.30f;  // Controls tunnel width (0.30 = wider tunnels for better cave systems)
    float tunnelDensity = std::abs(tunnelNoise) / tunnelRadius;  // 0.0 at tunnel center, >1.0 outside
    tunnelDensity = std::min(1.0f, tunnelDensity);  // Clamp to [0, 1]

    // === Chamber System (larger cave rooms) ===
    // Creates open spaces that tunnels connect to
    float chamberNoise = m_caveNoise->GetNoise(worldX, worldY * 0.5f, worldZ);
    float chamberDensity = mapNoiseTo01(chamberNoise);

    // === Combine tunnel and chamber systems ===
    // Use minimum (air wins) to connect tunnels to chambers
    float combinedDensity = std::min(tunnelDensity, chamberDensity);

    // === Surface proximity filter ===
    // Make caves rarer near the surface (top 20 blocks below terrain height)
    int terrainHeight = getTerrainHeightAt(worldX, worldZ);
    float depthBelowSurface = terrainHeight - worldY;

    if (depthBelowSurface < 20.0f && depthBelowSurface > 0.0f) {
        // Near surface - make caves rarer
        float surfaceProximity = 1.0f - (depthBelowSurface / 20.0f);  // 1.0 at surface, 0.0 at depth 20
        combinedDensity = combinedDensity + surfaceProximity * (1.0f - combinedDensity);  // Push towards solid
    }

    // Cave threshold: < 0.45 = air (cave), >= 0.45 = solid
    // We return density where higher = more solid
    return combinedDensity;
}

bool BiomeMap::isUndergroundBiomeAt(float worldX, float worldY, float worldZ) {
    // Underground chambers generate in deep caves for exploration gameplay
    // Extended range to Y=-200 to Y=200 for deep underground cave systems
    if (worldY < -200.0f || worldY > 200.0f) {
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

uint64_t BiomeMap::coordsToKey3D(int x, int y, int z) const {
    // Pack 3D coordinates into 64-bit key using memcpy to handle negative numbers correctly
    // Use upper 32 bits for (x,y) and lower 32 bits for z with a simple hash
    uint32_t ux, uy, uz;
    std::memcpy(&ux, &x, sizeof(uint32_t));
    std::memcpy(&uy, &y, sizeof(uint32_t));
    std::memcpy(&uz, &z, sizeof(uint32_t));

    // Combine using XOR and shifts to create unique 64-bit key
    uint64_t key = (static_cast<uint64_t>(ux) << 32) | static_cast<uint64_t>(uz);
    key ^= (static_cast<uint64_t>(uy) << 16);  // XOR in Y coordinate
    return key;
}

const Biome* BiomeMap::selectBiome(float temperature, float moisture) {
    auto& biomeRegistry = BiomeRegistry::getInstance();

    if (biomeRegistry.getBiomeCount() == 0) {
        std::cerr << "No biomes loaded!" << std::endl;
        return nullptr;
    }

    // Single-pass algorithm: find best matching biome in one iteration
    // Increased tolerance for smoother biome blending and natural transitions
    const float TOLERANCE = 25.0f;  // Wider tolerance = more gradual biome transitions
    const Biome* bestBiome = nullptr;
    float bestWeight = -1.0f;
    const Biome* closestBiome = nullptr;
    float closestDist = std::numeric_limits<float>::max();

    for (int i = 0; i < biomeRegistry.getBiomeCount(); i++) {
        const Biome* biome = biomeRegistry.getBiomeByIndex(i);
        if (!biome) continue;

        // Calculate distance from biome's preferred temp/moisture
        float tempDist = std::abs(temperature - biome->temperature);
        float moistureDist = std::abs(moisture - biome->moisture);
        float totalDist = tempDist + moistureDist;

        // Track closest biome for fallback
        if (totalDist < closestDist) {
            closestDist = totalDist;
            closestBiome = biome;
        }

        // Early exit: perfect match (or very close)
        if (tempDist <= 1.0f && moistureDist <= 1.0f) {
            return biome;  // Found perfect match - no need to continue
        }

        // If within tolerance, calculate weight and track best
        if (tempDist <= TOLERANCE && moistureDist <= TOLERANCE) {
            float proximityWeight = 1.0f - (tempDist + moistureDist) / (TOLERANCE * 2.0f);
            float rarityWeight = biome->biome_rarity_weight / 50.0f;
            float totalWeight = proximityWeight * rarityWeight;

            if (totalWeight > bestWeight) {
                bestWeight = totalWeight;
                bestBiome = biome;
            }
        }
    }

    // Return best matching biome, or closest if none within tolerance
    return (bestBiome != nullptr) ? bestBiome : closestBiome;
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
