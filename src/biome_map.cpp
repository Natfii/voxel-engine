#include "biome_map.h"
#include "terrain_constants.h"
#include "logger.h"
#include <algorithm>
#include <cmath>
#include <cstring>

BiomeMap::BiomeMap(int seed, float tempBias, float moistBias, float ageBias,
                   int minTemp, int maxTemp, int minMoisture, int maxMoisture)
    : m_temperatureBias(tempBias), m_moistureBias(moistBias), m_ageBias(ageBias),
      m_minTemperature(minTemp), m_maxTemperature(maxTemp),
      m_minMoisture(minMoisture), m_maxMoisture(maxMoisture) {
    // Temperature noise - MASSIVE scale for truly expansive biomes
    // Research-based: Minecraft 1.18+ uses ~0.00025 scale for climate zones
    // Lower frequency = wider biomes (0.00008 = ~12500 block features, truly massive)
    m_temperatureNoise = std::make_unique<FastNoiseLite>(seed);
    m_temperatureNoise->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_temperatureNoise->SetFractalType(FastNoiseLite::FractalType_FBm);
    m_temperatureNoise->SetFractalOctaves(3);  // Fewer octaves = smoother, less variation
    m_temperatureNoise->SetFrequency(0.00008f);  // Massive biomes: 5000-15000 blocks wide

    // Temperature variation - minimal local changes for stable biomes
    // This should only add subtle variation within a biome, not change the biome itself
    m_temperatureVariation = std::make_unique<FastNoiseLite>(seed + 1000);
    m_temperatureVariation->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_temperatureVariation->SetFractalOctaves(2);
    m_temperatureVariation->SetFrequency(0.003f);  // Very subtle local variation

    // Moisture noise - MASSIVE scale for expansive wet/dry zones
    m_moistureNoise = std::make_unique<FastNoiseLite>(seed + 100);
    m_moistureNoise->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_moistureNoise->SetFractalType(FastNoiseLite::FractalType_FBm);
    m_moistureNoise->SetFractalOctaves(3);  // Fewer octaves = smoother
    m_moistureNoise->SetFrequency(0.0001f);  // Massive biomes: 5000-15000 blocks wide

    // Moisture variation - minimal local changes
    m_moistureVariation = std::make_unique<FastNoiseLite>(seed + 1100);
    m_moistureVariation->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_moistureVariation->SetFractalOctaves(2);
    m_moistureVariation->SetFrequency(0.004f);  // Very subtle local variation

    // Terrain height noise - SMOOTHER for gradual landscapes
    // UPDATED (2025-11-25): Lower frequency for less valleys, more gradual terrain
    m_terrainNoise = std::make_unique<FastNoiseLite>(seed + 200);
    m_terrainNoise->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_terrainNoise->SetFractalType(FastNoiseLite::FractalType_FBm);
    m_terrainNoise->SetFractalOctaves(6);       // Was 5 - more octaves = smoother blending
    m_terrainNoise->SetFractalLacunarity(2.0f);
    m_terrainNoise->SetFractalGain(0.45f);      // Was 0.5 - lower = less harsh detail
    m_terrainNoise->SetFrequency(0.006f);       // Was 0.015 - MUCH lower for grand terrain
                                                 // 0.006 = ~167-block features (gradual hills)

    // Mountain range noise - creates VERY wide mountain ranges
    // Lower frequency = wider mountain ranges (0.0004 = ~2500 block features)
    m_mountainRangeNoise = std::make_unique<FastNoiseLite>(seed + 500);
    m_mountainRangeNoise->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_mountainRangeNoise->SetFractalType(FastNoiseLite::FractalType_Ridged);  // Ridged for range shapes
    m_mountainRangeNoise->SetFractalOctaves(4);
    m_mountainRangeNoise->SetFractalLacunarity(2.0f);
    m_mountainRangeNoise->SetFractalGain(0.5f);
    m_mountainRangeNoise->SetFrequency(0.0004f);  // Even lower - mountains 2000+ blocks wide

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
    m_caveTunnelNoise->SetFractalOctaves(4);  // More octaves = more detailed winding
    m_caveTunnelNoise->SetFrequency(0.025f);  // Slightly higher for more varied tunnel paths

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

    // Base temperature from MASSIVE scale noise (dominant factor)
    float baseTemp = m_temperatureNoise->GetNoise(worldX, worldZ);

    // Add very subtle variation for natural feel within biomes (minimal influence)
    float variation = m_temperatureVariation->GetNoise(worldX, worldZ);

    // Combine: 90% base + 10% variation (base noise dominates to prevent rapid biome changes)
    float combined = (baseTemp * 0.90f) + (variation * 0.10f);

    // Add distance-based temperature gradient for variety as you travel far
    // Distance from world origin (0, 0)
    float distanceFromOrigin = std::sqrt(worldX * worldX + worldZ * worldZ);

    // Create gradual temperature shift based on distance
    // Use multiple sine waves at different scales for natural variation
    float distanceScale1 = std::sin(distanceFromOrigin * 0.0002f) * 0.15f;  // Large-scale shift
    float distanceScale2 = std::sin(distanceFromOrigin * 0.0005f) * 0.08f;  // Medium-scale shift
    float distanceInfluence = distanceScale1 + distanceScale2;

    // Apply distance influence (multiply to stay within [-1, 1] range)
    combined = combined * (1.0f + distanceInfluence * 0.5f);
    combined = std::clamp(combined, -1.0f, 1.0f);  // Ensure we stay in valid range

    // Apply temperature bias from menu (-1.0 to +1.0)
    combined = std::clamp(combined + m_temperatureBias, -1.0f, 1.0f);

    // Map from [-1, 1] to actual biome temperature range
    // This ensures all biomes have equal representation in the world
    return mapNoiseToRange(combined, static_cast<float>(m_minTemperature), static_cast<float>(m_maxTemperature));
}

float BiomeMap::getMoistureAt(float worldX, float worldZ) {
    // FastNoiseLite is thread-safe for reads - no mutex needed

    // Base moisture from MASSIVE scale noise (dominant factor)
    float baseMoisture = m_moistureNoise->GetNoise(worldX, worldZ);

    // Add very subtle variation for natural feel within biomes (minimal influence)
    float variation = m_moistureVariation->GetNoise(worldX, worldZ);

    // Combine: 90% base + 10% variation (base noise dominates)
    float combined = (baseMoisture * 0.90f) + (variation * 0.10f);

    // Apply moisture bias from menu (-1.0 to +1.0)
    combined = std::clamp(combined + m_moistureBias, -1.0f, 1.0f);

    // Map from [-1, 1] to actual biome moisture range
    // This ensures all biomes have equal representation in the world
    return mapNoiseToRange(combined, static_cast<float>(m_minMoisture), static_cast<float>(m_maxMoisture));
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

    // ============================================================================
    // TERRAIN OVERHAUL (2025-11-25): Smoother, more natural terrain
    // ============================================================================
    // - Reduced base height variation for flatter landscapes
    // - Power curve to flatten valleys while keeping gentle peaks
    // - Mountain range noise for wide, gradual mountain slopes
    // ============================================================================

    // Use biome's age to control terrain roughness
    // Age 0 (young) = moderate variation
    // Age 100 (old) = very flat, plains-like (minimal variation)
    float ageNormalized = biome->age / 100.0f;  // 0.0 to 1.0

    // Apply age bias from menu (-1.0 = flatter, +1.0 = more mountainous)
    ageNormalized = std::clamp(ageNormalized - m_ageBias, 0.0f, 1.0f);

    // UPDATED: Reduced base variation for flatter terrain
    // Young terrain (age=0): variation = 12 blocks (was 20)
    // Old terrain (age=100): variation = 2 blocks (was 5)
    float heightVariation = 12.0f - (ageNormalized * 10.0f);  // Range: 12 to 2

    // Apply biome's height multiplier for special terrain (mountains, etc.)
    float baseHeightMultiplier = biome->height_multiplier;

    // NEW: For mountainous biomes, use mountain range noise for gradual slopes
    if (baseHeightMultiplier > 1.5f) {
        // Sample mountain range noise for gradual, wide mountains
        float rangeNoise = m_mountainRangeNoise->GetNoise(worldX, worldZ);
        rangeNoise = (rangeNoise + 1.0f) * 0.5f;  // Map to [0, 1]

        // Mountains only rise where range noise is high
        // This creates sharper peaks with steeper slopes
        // Lower power = sharper transition from plains to peaks (was 1.5, now 0.9)
        float mountainInfluence = std::pow(rangeNoise, 0.9f);

        // Scale the height multiplier by mountain influence
        // At mountain edges: multiplier ~1.0 (normal terrain)
        // At mountain centers/peaks: multiplier = full height_multiplier
        baseHeightMultiplier = 1.0f + (baseHeightMultiplier - 1.0f) * mountainInfluence;

        // Also apply biome size-based scaling for extra tall central peaks
        // Cache mountain density at 32-block resolution to avoid expensive sampling
        int mountainRegionX = static_cast<int>(worldX) >> 5;
        int mountainRegionZ = static_cast<int>(worldZ) >> 5;
        uint64_t mountainKey = coordsToKey(mountainRegionX, mountainRegionZ);

        float sizeScaling = 1.0f;
        bool needsCompute = false;

        // Check mountain density cache first (read lock only)
        {
            std::shared_lock<std::shared_mutex> lock(m_mountainCacheMutex);
            auto it = m_mountainDensityCache.find(mountainKey);
            if (it != m_mountainDensityCache.end()) {
                sizeScaling = it->second;
            } else {
                needsCompute = true;
            }
        }
        // shared_lock released here before any computation

        // Cache miss - compute outside the lock to avoid deadlock
        if (needsCompute) {
            // Sample larger area to detect wide mountain ranges (was 500, now 800)
            const float sampleRadius = 800.0f;
            int mountainCount = 0;
            const int totalSamples = 8;

            for (int i = 0; i < totalSamples; i++) {
                float angle = (i / float(totalSamples)) * 2.0f * 3.14159f;
                float sampleX = worldX + std::cos(angle) * sampleRadius;
                float sampleZ = worldZ + std::sin(angle) * sampleRadius;

                const Biome* sampleBiome = getBiomeAt(sampleX, sampleZ);
                if (sampleBiome && sampleBiome->height_multiplier > 1.5f) {
                    mountainCount++;
                }
            }

            float mountainDensity = mountainCount / float(totalSamples);
            // Increased scaling range: small mountain patches = 0.7x, large mountain ranges = 1.6x
            // This allows for much taller peaks in wide mountain areas
            sizeScaling = 0.7f + (mountainDensity * 0.9f);  // Range: 0.7x to 1.6x

            // Store in cache (write lock, no other locks held)
            std::unique_lock<std::shared_mutex> writeLock(m_mountainCacheMutex);
            if (m_mountainDensityCache.size() < MAX_CACHE_SIZE) {
                m_mountainDensityCache[mountainKey] = sizeScaling;
            }
        }

        baseHeightMultiplier *= sizeScaling;
    }

    heightVariation *= baseHeightMultiplier;

    // NEW: Apply power curve to flatten valleys while keeping gentle peaks
    // Power > 1.0 pushes middle values down, creating flatter lowlands
    // This reduces the "choppy" look of constant hills/valleys
    float normalizedNoise = (noise + 1.0f) * 0.5f;  // Map [-1, 1] to [0, 1]
    normalizedNoise = std::pow(normalizedNoise, 1.3f);  // Gentle flattening curve
    noise = normalizedNoise * 2.0f - 1.0f;  // Map back to [-1, 1]

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

float BiomeMap::getTerrainNoise(float worldX, float worldZ) {
    // Return raw terrain noise for snow line variation and other effects
    return m_terrainNoise->GetNoise(worldX, worldZ);
}

float BiomeMap::getCaveDensityAt(float worldX, float worldY, float worldZ, int terrainHeight) {
    // FastNoiseLite is thread-safe for reads - no mutex needed
    // NOTE: terrainHeight passed as parameter to avoid redundant calculation (was called 32x per column!)

    // MOUNTAIN FIX: Check biome to suppress caves in mountains
    // Mountains should be mostly solid, especially at high elevations
    const Biome* biome = getBiomeAt(worldX, worldZ);
    float biomeCaveSuppression = 0.0f;  // 0.0 = normal caves, 1.0 = no caves

    if (biome && biome->height_multiplier > 1.5f) {
        // This is a mountainous biome - reduce caves significantly
        // Higher elevations have even fewer caves (mountains are solid rock)
        float elevationAboveBase = worldY - 64.0f;  // Base elevation where mountains start
        if (elevationAboveBase > 0.0f) {
            // Suppress caves more as we go higher up the mountain
            // At elevation +0: 50% cave suppression
            // At elevation +50: 85% cave suppression
            // At elevation +100+: 95% cave suppression (nearly solid)
            biomeCaveSuppression = 0.5f + (std::min(elevationAboveBase, 100.0f) / 100.0f) * 0.45f;
        } else {
            // Below mountain base: still some suppression (30%) for foothills
            biomeCaveSuppression = 0.3f;
        }
    }

    // === Primary Winding Tunnel System ===
    // Creates narrow, winding tunnels that snake through the underground
    // Uses "Perlin worms" technique: tunnels where abs(noise) is close to 0
    float tunnelNoise = m_caveTunnelNoise->GetNoise(worldX, worldY, worldZ);

    // Narrower tunnels for more realistic winding caves
    float tunnelRadius = 0.12f;  // Reduced from 0.30 for tighter, more winding tunnels
    float tunnelDensity = std::abs(tunnelNoise) / tunnelRadius;
    tunnelDensity = std::min(1.0f, tunnelDensity);

    // === Secondary Tunnel System (crosses primary for connectivity) ===
    // Rotated noise to create intersecting tunnel networks
    float tunnelNoise2 = m_caveTunnelNoise->GetNoise(worldX * 0.7f + 1000.0f, worldY * 1.3f, worldZ * 0.7f);
    float tunnelRadius2 = 0.10f;  // Even narrower secondary tunnels
    float tunnelDensity2 = std::abs(tunnelNoise2) / tunnelRadius2;
    tunnelDensity2 = std::min(1.0f, tunnelDensity2);

    // === Occasional Chamber System (rare large rooms) ===
    // Sparse chambers that tunnels occasionally open into
    float chamberNoise = m_caveNoise->GetNoise(worldX, worldY * 0.5f, worldZ);
    float chamberDensity = mapNoiseTo01(chamberNoise);

    // Make chambers much rarer by raising threshold
    chamberDensity = (chamberDensity < 0.25f) ? 0.0f : 1.0f;  // Only 25% of areas can be chambers

    // === Combine systems ===
    // Use minimum so tunnels and chambers connect
    float combinedDensity = std::min(std::min(tunnelDensity, tunnelDensity2), chamberDensity);

    // MOUNTAIN FIX: Apply biome cave suppression
    // Lerp towards 1.0 (solid) based on suppression amount
    combinedDensity = combinedDensity + biomeCaveSuppression * (1.0f - combinedDensity);

    // === Surface Entrance System ===
    // Allow caves to reach surface in some areas, creating natural entrances
    // Use passed terrainHeight parameter (already calculated once per column)
    float depthBelowSurface = terrainHeight - worldY;

    // Check if we're near surface (top 15 blocks)
    if (depthBelowSurface >= 0.0f && depthBelowSurface < 15.0f) {
        // Sample entrance noise to determine if this area should have cave entrance
        float entranceNoise = m_undergroundChamberNoise->GetNoise(worldX * 0.05f, 0.0f, worldZ * 0.05f);
        float entranceChance = mapNoiseTo01(entranceNoise);

        // Only 15% of surface areas have cave entrances
        if (entranceChance > 0.85f) {
            // This area can have a cave entrance - don't block caves near surface
            // Gradually transition from open entrance to normal cave
            float transitionDepth = depthBelowSurface / 8.0f;  // Transition over 8 blocks
            combinedDensity = combinedDensity * (0.3f + 0.7f * transitionDepth);
        } else {
            // No entrance here - gradually seal off caves near surface
            float surfaceProximity = 1.0f - (depthBelowSurface / 15.0f);
            combinedDensity = combinedDensity + surfaceProximity * (1.0f - combinedDensity);
        }
    }

    // Cave threshold: < 0.45 = air (cave), >= 0.45 = solid
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
        Logger::error() << "No biomes loaded!";
        return nullptr;
    }

    // Single-pass algorithm: find best matching biome in one iteration
    // Reduced tolerance to prevent excessive biome overlap
    // With biomes at temp: 5, 15, 35, 55, 60, 90, tolerance of 12 prevents most overlap
    const float TOLERANCE = 12.0f;  // Tight tolerance for distinct biomes
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
