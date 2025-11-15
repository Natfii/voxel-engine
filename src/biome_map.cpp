#include "biome_map.h"
#include "terrain_constants.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

BiomeMap::BiomeMap(int seed) {
    // ==================== BIOME SELECTION NOISE SYSTEM ====================
    // Multi-layer noise system for rich, varied biome selection
    // Inspired by Minecraft 1.18+ terrain generation with multiple noise dimensions
    //
    // Design Philosophy:
    // - Layer 1: Temperature & Moisture (primary climate axes)
    // - Layer 2: Weirdness (creates unusual biome combinations and variety)
    // - Layer 3: Erosion (influences terrain roughness and transitions)
    // - Each layer has base noise (large-scale) + detail noise (local variation)
    //
    // Frequency Guide (UPDATED for wider biomes):
    // - 0.0003-0.0005: EXTRA WIDE scale (2000-3333 blocks) - massive biome zones (4-8+ chunks)
    // - 0.002-0.004: Large scale (250-500 blocks) - large variation within biomes
    // - 0.005-0.015: Medium scale (70-200 blocks) - local variation within biomes
    // - 0.02-0.05: Small scale (20-50 blocks) - fine detail and transitions

    // === LAYER 1: TEMPERATURE (Primary Climate Axis) ===
    // Temperature noise - EXTRA WIDE scale for biomes spanning 4-8+ chunks
    // Lower frequency = wider biomes (0.0003 = ~3333 block features, 3x larger than before)
    m_temperatureNoise = std::make_unique<FastNoiseLite>(seed);
    m_temperatureNoise->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_temperatureNoise->SetFractalType(FastNoiseLite::FractalType_FBm);
    m_temperatureNoise->SetFractalOctaves(5);  // Increased from 4 for more detail
    m_temperatureNoise->SetFractalLacunarity(2.2f);  // Slightly more detail per octave
    m_temperatureNoise->SetFractalGain(0.55f);  // Balanced detail contribution
    m_temperatureNoise->SetFrequency(0.0003f);  // REDUCED 3x: Extra wide biomes (2000-3333 block features)

    // Temperature variation - adds local temperature changes within biomes
    m_temperatureVariation = std::make_unique<FastNoiseLite>(seed + 1000);
    m_temperatureVariation->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_temperatureVariation->SetFractalType(FastNoiseLite::FractalType_FBm);
    m_temperatureVariation->SetFractalOctaves(3);
    m_temperatureVariation->SetFrequency(0.003f);  // REDUCED 4x: Larger variation features (~333 blocks)

    // === LAYER 2: MOISTURE (Primary Climate Axis) ===
    // Moisture noise - EXTRA WIDE scale for wide wet/dry zones spanning 4-8+ chunks
    m_moistureNoise = std::make_unique<FastNoiseLite>(seed + 100);
    m_moistureNoise->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_moistureNoise->SetFractalType(FastNoiseLite::FractalType_FBm);
    m_moistureNoise->SetFractalOctaves(5);  // Increased from 4 for more detail
    m_moistureNoise->SetFractalLacunarity(2.2f);
    m_moistureNoise->SetFractalGain(0.55f);
    m_moistureNoise->SetFrequency(0.0004f);  // REDUCED ~3x: Slightly different from temperature for variety

    // Moisture variation - adds local moisture changes within biomes
    m_moistureVariation = std::make_unique<FastNoiseLite>(seed + 1100);
    m_moistureVariation->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_moistureVariation->SetFractalType(FastNoiseLite::FractalType_FBm);
    m_moistureVariation->SetFractalOctaves(3);
    m_moistureVariation->SetFrequency(0.0035f);  // REDUCED 4x: Larger variation features (~285 blocks)

    // === LAYER 3: WEIRDNESS (Biome Variety & Unusual Combinations) ===
    // Weirdness creates interesting biome variety and prevents monotonous patterns
    // Similar to Minecraft 1.18+ "continentalness" or "weirdness" parameter
    m_weirdnessNoise = std::make_unique<FastNoiseLite>(seed + 2000);
    m_weirdnessNoise->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_weirdnessNoise->SetFractalType(FastNoiseLite::FractalType_FBm);
    m_weirdnessNoise->SetFractalOctaves(4);
    m_weirdnessNoise->SetFractalLacunarity(2.5f);  // More dramatic variation
    m_weirdnessNoise->SetFractalGain(0.6f);
    m_weirdnessNoise->SetFrequency(0.0003f);  // REDUCED ~3x: Extra wide continental patterns

    // Weirdness detail - local strange biome variations
    m_weirdnessDetail = std::make_unique<FastNoiseLite>(seed + 2100);
    m_weirdnessDetail->SetNoiseType(FastNoiseLite::NoiseType_Perlin);  // Perlin for smoother detail
    m_weirdnessDetail->SetFractalType(FastNoiseLite::FractalType_FBm);
    m_weirdnessDetail->SetFractalOctaves(2);
    m_weirdnessDetail->SetFrequency(0.002f);  // REDUCED 4x: Larger weird patches (~500 blocks)

    // === LAYER 4: EROSION (Terrain Roughness & Transitions) ===
    // Erosion influences how rough or smooth terrain transitions are
    m_erosionNoise = std::make_unique<FastNoiseLite>(seed + 3000);
    m_erosionNoise->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_erosionNoise->SetFractalType(FastNoiseLite::FractalType_Ridged);  // Ridged for erosion patterns
    m_erosionNoise->SetFractalOctaves(4);
    m_erosionNoise->SetFractalLacunarity(2.3f);
    m_erosionNoise->SetFractalGain(0.5f);
    m_erosionNoise->SetFrequency(0.0004f);  // REDUCED ~3x: Extra wide erosion patterns

    // Erosion detail - local terrain roughness
    m_erosionDetail = std::make_unique<FastNoiseLite>(seed + 3100);
    m_erosionDetail->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_erosionDetail->SetFractalType(FastNoiseLite::FractalType_FBm);
    m_erosionDetail->SetFractalOctaves(3);
    m_erosionDetail->SetFrequency(0.0025f);  // REDUCED 4x: Larger detail features (~400 blocks)

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

    // === 3D BIOME INFLUENCE SYSTEM: Altitude-based Noise ===
    // Altitude variation - adds natural variation to altitude-based transitions
    // Prevents uniform horizontal lines at biome transition heights
    m_altitudeVariation = std::make_unique<FastNoiseLite>(seed + 5000);
    m_altitudeVariation->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_altitudeVariation->SetFractalType(FastNoiseLite::FractalType_FBm);
    m_altitudeVariation->SetFractalOctaves(3);
    m_altitudeVariation->SetFrequency(0.02f);  // Medium-scale variation (50-block features)

    // Snow line variation - creates natural, irregular snow coverage
    // Higher frequency creates more varied, realistic mountain peaks
    m_snowLineNoise = std::make_unique<FastNoiseLite>(seed + 5100);
    m_snowLineNoise->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_snowLineNoise->SetFractalType(FastNoiseLite::FractalType_FBm);
    m_snowLineNoise->SetFractalOctaves(4);
    m_snowLineNoise->SetFrequency(0.03f);  // Creates natural snow patches

    // === VORONOI CENTER SYSTEM (NEW: Agent 24) ===
    // Initialize the Voronoi-based biome clustering system
    // This provides an alternative to noise-based biome selection
    // Disabled by default - can be enabled with setVoronoiMode(true)
    m_seed = seed;
    m_voronoi = std::make_unique<BiomeVoronoi>(seed);
    m_useVoronoiMode = false;  // Start with traditional noise-based selection

    // Initialize RNG for feature blending
    m_featureRng.seed(seed + 88888);
    // Initialize with balanced transition profile (default)
    m_transitionProfile = BiomeTransition::PROFILE_BALANCED;

    std::cout << "BiomeMap initialized with transition profile: " << m_transitionProfile.name << std::endl;
    std::cout << "Voronoi center system initialized (disabled by default)" << std::endl;
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

float BiomeMap::getWeirdnessAt(float worldX, float worldZ) {
    // FastNoiseLite is thread-safe for reads - no mutex needed

    // Base weirdness from large-scale continental patterns
    float baseWeirdness = m_weirdnessNoise->GetNoise(worldX, worldZ);

    // Add local weird variations
    float detail = m_weirdnessDetail->GetNoise(worldX, worldZ);

    // Combine: 65% base + 35% detail (more detail for interesting variety)
    float combined = (baseWeirdness * 0.65f) + (detail * 0.35f);

    // Map from [-1, 1] to [0, 100]
    return mapNoiseToRange(combined, 0.0f, 100.0f);
}

float BiomeMap::getErosionAt(float worldX, float worldZ) {
    // FastNoiseLite is thread-safe for reads - no mutex needed

    // Base erosion from large-scale patterns (ridged for dramatic erosion)
    float baseErosion = m_erosionNoise->GetNoise(worldX, worldZ);

    // Add local erosion detail
    float detail = m_erosionDetail->GetNoise(worldX, worldZ);

    // Combine: 60% base + 40% detail
    float combined = (baseErosion * 0.6f) + (detail * 0.4f);

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

    // Not in cache - compute all noise values for multi-dimensional biome selection
    float temperature = getTemperatureAt(worldX, worldZ);
    float moisture = getMoistureAt(worldX, worldZ);
    float weirdness = getWeirdnessAt(worldX, worldZ);
    float erosion = getErosionAt(worldX, worldZ);

    // Select biome based on all four noise dimensions
    const Biome* biome = selectBiome(temperature, moisture, weirdness, erosion);

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

        m_biomeCache[key] = {biome, temperature, moisture, weirdness, erosion};
    }

    return biome;
}

// ==================== Per-Biome Noise Generation ====================

/**
 * Generate noise with custom octaves, frequency, lacunarity, and gain
 * This allows each biome to have unique terrain roughness characteristics
 *
 * @param x World X coordinate
 * @param z World Z coordinate
 * @param octaves Number of noise octaves (more = more detail)
 * @param baseFrequency Base frequency multiplier
 * @param lacunarity Frequency multiplier per octave
 * @param gain Amplitude multiplier per octave
 * @return Noise value in range [-1, 1]
 */
float BiomeMap::generatePerBiomeNoise(float x, float z, int octaves,
                                      float baseFrequency, float lacunarity, float gain) const {
    // Manual FBm (Fractional Brownian Motion) implementation
    // This gives us full control over octaves/lacunarity/gain per-call
    float amplitude = 1.0f;
    float frequency = baseFrequency;
    float total = 0.0f;
    float maxValue = 0.0f;  // For normalization

    for (int i = 0; i < octaves; i++) {
        // Sample base terrain noise at this octave's frequency
        // Thread-safe: FastNoiseLite is safe for concurrent reads
        float noiseValue = m_terrainNoise->GetNoise(x * frequency, z * frequency);

        total += noiseValue * amplitude;
        maxValue += amplitude;

        // Update for next octave
        amplitude *= gain;
        frequency *= lacunarity;
    }

    // Normalize to [-1, 1] range
    if (maxValue > 0.0f) {
        total /= maxValue;
    }

    return total;
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

    // Not in cache - compute blended height from multiple biomes
    // ==================== MULTI-BIOME HEIGHT BLENDING ====================
    // Get all biome influences at this location for smooth transitions
    std::vector<BiomeInfluence> influences = getBiomeInfluences(worldX, worldZ);

    if (influences.empty()) {
        // Fallback: no biomes found (should never happen)
        return BASE_HEIGHT;
    }

    // FastNoiseLite is thread-safe for reads - no mutex needed
    float noise = m_terrainNoise->GetNoise(worldX, worldZ);

    // === BLEND HEIGHT CONTRIBUTIONS FROM EACH BIOME ===
    // Each biome contributes to the final height based on its influence weight
    // This creates smooth transitions without sudden cliffs at biome borders
    float blendedHeight = 0.0f;

    for (const auto& influence : influences) {
        const Biome* biome = influence.biome;
        if (!biome) continue;

        // === PER-BIOME HEIGHT VARIATION SYSTEM ===
        // Use biome-specific noise frequency for different terrain types
        float biomeNoise = m_terrainNoise->GetNoise(worldX * biome->height_noise_frequency,
                                                      worldZ * biome->height_noise_frequency);

        // Calculate height variation using biome-specific min/max ranges
        float heightVariation;
        if (biomeNoise > 0.0f) {
            heightVariation = biome->height_variation_min +
                            (biomeNoise * (biome->height_variation_max - biome->height_variation_min));
        } else {
            heightVariation = biome->height_variation_min +
                            (std::abs(biomeNoise) * (biome->height_variation_max - biome->height_variation_min));
        }

        // Apply height multiplier (backward compatibility)
        heightVariation *= biome->height_multiplier;

        // Add specialized terrain features (peaks and valleys)
        float specialFeatures = 0.0f;

        // Mountain peaks (positive noise regions)
        if (biome->peak_height > 0 && biomeNoise > 0.3f) {
            float peakStrength = (biomeNoise - 0.3f) / 0.7f;
            specialFeatures += biome->peak_height * peakStrength;
        }

        // Deep valleys (negative noise regions)
        if (biome->valley_depth < 0 && biomeNoise < -0.3f) {
            float valleyStrength = (std::abs(biomeNoise) - 0.3f) / 0.7f;
            specialFeatures += biome->valley_depth * valleyStrength;
        }

        // Calculate final biome height with all components
        float biomeHeight = BASE_HEIGHT + biome->base_height_offset +
                           (biomeNoise * heightVariation) + specialFeatures;

        // Add weighted contribution to blended height
        blendedHeight += biomeHeight * influence.weight;
    }

    // Convert to integer height (use rounding for smooth transitions)
    int height = static_cast<int>(std::round(blendedHeight));


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

const Biome* BiomeMap::selectBiome(float temperature, float moisture, float weirdness, float erosion) {
    auto& biomeRegistry = BiomeRegistry::getInstance();

    if (biomeRegistry.getBiomeCount() == 0) {
        std::cerr << "No biomes loaded!" << std::endl;
        return nullptr;
    }

    // ==================== ENHANCED MULTI-DIMENSIONAL BIOME SELECTION ====================
    // Uses 4D noise space (temperature, moisture, weirdness, erosion) for rich biome variety
    //
    // Selection Strategy:
    // 1. Primary Match: Temperature RANGE & Moisture (most important for climate)
    //    - Now uses temperature RANGES for more natural biome distribution
    //    - Biomes have preferred temperature ranges where they spawn optimally
    // 2. Secondary Influence: Weirdness (creates variety, allows unusual biomes)
    // 3. Tertiary Influence: Erosion (subtle terrain transition effects)
    // 4. Weight by rarity to balance common vs. rare biomes

    const float PRIMARY_TOLERANCE = 20.0f;    // Tolerance for temp/moisture matching
    const float WEIRDNESS_INFLUENCE = 0.3f;   // How much weirdness affects selection (30%)
    const float EROSION_INFLUENCE = 0.15f;    // How much erosion affects selection (15%)

    const Biome* bestBiome = nullptr;
    float bestWeight = -1.0f;
    const Biome* closestBiome = nullptr;
    float closestDist = std::numeric_limits<float>::max();

    for (int i = 0; i < biomeRegistry.getBiomeCount(); i++) {
        const Biome* biome = biomeRegistry.getBiomeByIndex(i);
        if (!biome) continue;

        // === PRIMARY MATCHING: Temperature Range & Moisture ===
        // Temperature matching now uses range-based system
        int tempMin = biome->getEffectiveMinTemp();
        int tempMax = biome->getEffectiveMaxTemp();

        float tempDist;
        if (temperature >= tempMin && temperature <= tempMax) {
            // Inside temperature range - no distance penalty, optimal for this biome
            tempDist = 0.0f;
        } else if (temperature < tempMin) {
            // Below minimum - distance from min
            tempDist = tempMin - temperature;
        } else {
            // Above maximum - distance from max
            tempDist = temperature - tempMax;
        }

        float moistureDist = std::abs(moisture - biome->moisture);
        float primaryDist = tempDist + moistureDist;

        // Track closest biome for fallback (based on primary dimensions only)
        if (primaryDist < closestDist) {
            closestDist = primaryDist;
            closestBiome = biome;
        }

        // Early exit: perfect temperature match (within range) and near-perfect moisture
        if (tempDist == 0.0f && moistureDist <= 2.0f) {
            return biome;  // Found excellent match within ideal temperature range
        }

        // === MULTI-DIMENSIONAL WEIGHTING ===
        // Only consider biomes within primary tolerance
        if (tempDist <= PRIMARY_TOLERANCE && moistureDist <= PRIMARY_TOLERANCE) {
            // Base proximity weight from temperature & moisture
            float proximityWeight = 1.0f - (primaryDist / (PRIMARY_TOLERANCE * 2.0f));

            // Weirdness influence: allows "weird" biome placements
            // High weirdness (>60) increases variety, low weirdness (<40) keeps it normal
            float weirdnessFactor = 1.0f;
            if (weirdness > 60.0f) {
                // High weirdness: boost unusual biomes (low rarity weight)
                weirdnessFactor = 1.0f + ((weirdness - 60.0f) / 40.0f) * WEIRDNESS_INFLUENCE;
                if (biome->biome_rarity_weight < 30) {
                    weirdnessFactor *= 1.5f;  // Extra boost for rare biomes in weird areas
                }
            } else if (weirdness < 40.0f) {
                // Low weirdness: favor common biomes
                weirdnessFactor = 1.0f + ((40.0f - weirdness) / 40.0f) * WEIRDNESS_INFLUENCE;
                if (biome->biome_rarity_weight > 70) {
                    weirdnessFactor *= 1.3f;  // Boost common biomes in normal areas
                }
            }

            // Erosion influence: subtle effect on terrain-dependent biomes
            // Use biome's age as a correlation with erosion
            float erosionFactor = 1.0f;
            float ageSimilarity = 1.0f - (std::abs(erosion - biome->age) / 100.0f);
            erosionFactor = 1.0f + (ageSimilarity * EROSION_INFLUENCE);

            // Rarity weight: balance common and rare biomes
            float rarityWeight = biome->biome_rarity_weight / 50.0f;

            // Combine all factors
            float totalWeight = proximityWeight * weirdnessFactor * erosionFactor * rarityWeight;

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

// ==================== Biome Blending System ====================

void BiomeMap::setTransitionProfile(const BiomeTransition::TransitionProfile& profile) {
    m_transitionProfile = profile;

    // Clear influence cache when profile changes since weights will be different
    {
        std::unique_lock<std::shared_mutex> lock(m_influenceCacheMutex);
        m_influenceCache.clear();
    }

    std::cout << "Biome transition profile set to: " << profile.name << std::endl;
    std::cout << "  - Search Radius: " << profile.searchRadius << std::endl;
    std::cout << "  - Blend Distance: " << profile.blendDistance << std::endl;
    std::cout << "  - Transition Type: " << static_cast<int>(profile.type) << std::endl;
    std::cout << "  - Max Biomes: " << profile.maxBiomes << std::endl;
    std::cout << "  - Sharpness: " << profile.sharpness << std::endl;
}


float BiomeMap::calculateInfluenceWeight(float distance, float rarityWeight) const {
    // Use the new configurable transition system
    return BiomeTransition::calculateTransitionWeight(distance, m_transitionProfile, rarityWeight);
}

std::vector<BiomeInfluence> BiomeMap::getBiomeInfluences(float worldX, float worldZ) {
    // Create a cache key (quantize to 8-block resolution for coarse caching)
    // This is coarser than biome cache (4 blocks) to reduce memory usage
    // while still providing smooth blending
    int quantizedX = static_cast<int>(worldX / 8.0f);
    int quantizedZ = static_cast<int>(worldZ / 8.0f);
    uint64_t key = coordsToKey(quantizedX, quantizedZ);

    // Check cache first (shared lock - parallel reads)
    {
        std::shared_lock<std::shared_mutex> lock(m_influenceCacheMutex);
        auto it = m_influenceCache.find(key);
        if (it != m_influenceCache.end()) {
            return it->second.influences;
        }
    }

    // Not in cache - compute influences

    // ==================== VORONOI CENTER MODE (NEW: Agent 24) ====================
    // If Voronoi mode is enabled, use center-based biome clustering
    if (m_useVoronoiMode && m_voronoi) {
        // Find nearest biome centers using Voronoi system
        auto centerDistances = m_voronoi->findNearestCenters(worldX, worldZ, m_transitionProfile.maxBiomes);

        if (centerDistances.empty()) {
            std::cerr << "Warning: No Voronoi centers found!" << std::endl;
            return {};
        }

        // Calculate influences based on distance to centers
        std::vector<BiomeInfluence> influences;
        influences.reserve(centerDistances.size());

        float minDistance = centerDistances[0].second;  // Distance to closest center
        float blendRadius = m_voronoi->getBlendRadius();
        float totalWeight = 0.0f;

        for (const auto& [center, distance] : centerDistances) {
            if (!center.biome) continue;

            // Calculate Voronoi weight with smooth falloff
            float weight = m_voronoi->calculateVoronoiWeight(distance, minDistance, blendRadius);

            if (weight > m_transitionProfile.minInfluence) {
                influences.emplace_back(center.biome, weight);
                totalWeight += weight;
            }
        }

        // Normalize weights to sum to 1.0
        if (totalWeight > 0.0001f) {
            for (auto& influence : influences) {
                influence.weight /= totalWeight;
            }
        }

        // Cache and return
        {
            std::unique_lock<std::shared_mutex> lock(m_influenceCacheMutex);
            if (m_influenceCache.size() < MAX_CACHE_SIZE) {
                m_influenceCache[key] = {influences};
            }
        }

        return influences;
    }

    // ==================== TRADITIONAL NOISE-BASED MODE ====================
    // Original noise-based biome selection (used when Voronoi mode is disabled)
    float temperature = getTemperatureAt(worldX, worldZ);
    float moisture = getMoistureAt(worldX, worldZ);

    auto& biomeRegistry = BiomeRegistry::getInstance();

    if (biomeRegistry.getBiomeCount() == 0) {
        std::cerr << "Warning: getBiomeInfluences() called with no biomes loaded!" << std::endl;
        return {};
    }

    // Find all biomes within search radius and calculate weights
    std::vector<BiomeInfluence> influences;
    influences.reserve(m_transitionProfile.maxBiomes);  // Pre-allocate for performance

    float totalWeight = 0.0f;

    for (int i = 0; i < biomeRegistry.getBiomeCount(); i++) {
        const Biome* biome = biomeRegistry.getBiomeByIndex(i);
        if (!biome) continue;

        // Calculate distance in temperature-moisture space
        // Temperature now uses range-based distance calculation
        int tempMin = biome->getEffectiveMinTemp();
        int tempMax = biome->getEffectiveMaxTemp();

        float tempDist;
        if (temperature >= tempMin && temperature <= tempMax) {
            // Inside temperature range - use distance from center for smooth blending
            float tempCenter = (tempMin + tempMax) / 2.0f;
            tempDist = std::abs(temperature - tempCenter) * 0.5f;  // Reduced penalty inside range
        } else if (temperature < tempMin) {
            // Below minimum - full distance from min
            tempDist = tempMin - temperature;
        } else {
            // Above maximum - full distance from max
            tempDist = temperature - tempMax;
        }

        float moistDist = std::abs(moisture - biome->moisture);
        float totalDist = std::sqrt(tempDist * tempDist + moistDist * moistDist);

        // Determine search radius (per-biome or global)
        float searchRadius = biome->falloffConfig.useCustomFalloff ?
                             biome->falloffConfig.customSearchRadius :
                             m_transitionProfile.searchRadius;

        // Only consider biomes within search radius
        if (totalDist <= searchRadius) {
            // Calculate influence weight using per-biome falloff if enabled
            float weight;
            if (biome->falloffConfig.useCustomFalloff) {
                // Use per-biome custom falloff configuration (Agent 23 enhancement)
                weight = BiomeFalloff::calculateBiomeFalloff(totalDist, biome->falloffConfig,
                                                             static_cast<float>(biome->biome_rarity_weight));
            } else {
                // Use global transition profile falloff
                weight = calculateInfluenceWeight(totalDist, static_cast<float>(biome->biome_rarity_weight));
            }

            // Only include influences above minimum threshold
            float minInfluence = biome->falloffConfig.useCustomFalloff ?
                                0.001f : // Custom falloffs use their own weight system
                                m_transitionProfile.minInfluence;

            if (weight > minInfluence) {
                influences.emplace_back(biome, weight);
                totalWeight += weight;
            }
        }
    }

    // Handle edge case: no biomes within range (should be rare)
    if (influences.empty() || totalWeight < 0.0001f) {
        // Fallback: find closest biome (using temperature ranges)
        const Biome* closestBiome = nullptr;
        float closestDist = std::numeric_limits<float>::max();

        for (int i = 0; i < biomeRegistry.getBiomeCount(); i++) {
            const Biome* biome = biomeRegistry.getBiomeByIndex(i);
            if (!biome) continue;

            // Use same temperature range logic as above for consistency
            int tempMin = biome->getEffectiveMinTemp();
            int tempMax = biome->getEffectiveMaxTemp();

            float tempDist;
            if (temperature >= tempMin && temperature <= tempMax) {
                float tempCenter = (tempMin + tempMax) / 2.0f;
                tempDist = std::abs(temperature - tempCenter) * 0.5f;
            } else if (temperature < tempMin) {
                tempDist = tempMin - temperature;
            } else {
                tempDist = temperature - tempMax;
            }

            float moistDist = std::abs(moisture - biome->moisture);
            float totalDist = std::sqrt(tempDist * tempDist + moistDist * moistDist);

            if (totalDist < closestDist) {
                closestDist = totalDist;
                closestBiome = biome;
            }
        }

        if (closestBiome) {
            influences.clear();
            influences.emplace_back(closestBiome, 1.0f);
            totalWeight = 1.0f;
        } else {
            // No biomes at all - this should never happen
            std::cerr << "Error: No biomes found in getBiomeInfluences()!" << std::endl;
            return {};
        }
    }

    // Normalize weights to sum to 1.0
    // This ensures that blended values (height, properties, etc.) are proper averages
    for (auto& influence : influences) {
        influence.weight /= totalWeight;
    }

    // Sort by weight (descending) for easier processing downstream
    // Dominant biomes appear first in the list
    std::sort(influences.begin(), influences.end(),
              [](const BiomeInfluence& a, const BiomeInfluence& b) {
                  return a.weight > b.weight;
              });

    // Limit to m_transitionProfile.maxBiomes for performance
    // Keep only the most influential biomes
    if (influences.size() > m_transitionProfile.maxBiomes) {
        // Re-normalize after trimming
        influences.resize(m_transitionProfile.maxBiomes);
        totalWeight = 0.0f;
        for (const auto& influence : influences) {
            totalWeight += influence.weight;
        }
        for (auto& influence : influences) {
            influence.weight /= totalWeight;
        }
    }

    // Cache the result (exclusive lock for write)
    {
        std::unique_lock<std::shared_mutex> lock(m_influenceCacheMutex);

        // Double-check: another thread may have cached it
        auto it = m_influenceCache.find(key);
        if (it != m_influenceCache.end()) {
            return it->second.influences;
        }

        // LRU-style eviction when cache is full
        if (m_influenceCache.size() >= MAX_CACHE_SIZE) {
            size_t removeCount = MAX_CACHE_SIZE / 5;  // Remove 20%
            auto removeIt = m_influenceCache.begin();
            for (size_t i = 0; i < removeCount && removeIt != m_influenceCache.end(); ++i) {
                removeIt = m_influenceCache.erase(removeIt);
            }
        }

        m_influenceCache[key] = {influences};
    }

    return influences;
}

// ==================== Feature Blending Functions ====================

float BiomeMap::getBlendedTreeDensity(float worldX, float worldZ) {
    // Get all biome influences at this position
    auto influences = getBiomeInfluences(worldX, worldZ);

    if (influences.empty()) {
        return 0.0f;  // No biomes, no trees
    }

    // Calculate weighted average of tree densities
    float blendedDensity = 0.0f;
    for (const auto& influence : influences) {
        if (influence.biome && influence.biome->trees_spawn) {
            blendedDensity += influence.biome->tree_density * influence.weight;
        }
    }

    return blendedDensity;
}

const Biome* BiomeMap::selectTreeBiome(float worldX, float worldZ) {
    // Get all biome influences at this position
    auto influences = getBiomeInfluences(worldX, worldZ);

    if (influences.empty()) {
        return nullptr;
    }

    // Filter to only biomes that allow tree spawning
    std::vector<BiomeInfluence> treeSpawningBiomes;
    for (const auto& influence : influences) {
        if (influence.biome && influence.biome->trees_spawn) {
            treeSpawningBiomes.push_back(influence);
        }
    }

    if (treeSpawningBiomes.empty()) {
        return nullptr;  // No biomes allow trees
    }

    // Re-normalize weights to sum to 1.0 (after filtering)
    float totalWeight = 0.0f;
    for (const auto& influence : treeSpawningBiomes) {
        totalWeight += influence.weight;
    }

    // Handle edge case
    if (totalWeight < 0.0001f) {
        return treeSpawningBiomes[0].biome;  // Fallback to first biome
    }

    // Normalize
    for (auto& influence : treeSpawningBiomes) {
        influence.weight /= totalWeight;
    }

    // Weighted random selection
    // Thread-safe RNG access
    float roll;
    {
        std::lock_guard<std::mutex> lock(m_rngMutex);
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        roll = dist(m_featureRng);
    }

    // Select biome based on cumulative weights
    float cumulative = 0.0f;
    for (const auto& influence : treeSpawningBiomes) {
        cumulative += influence.weight;
        if (roll <= cumulative) {
            return influence.biome;
        }
    }

    // Fallback (should rarely happen due to floating point precision)
    return treeSpawningBiomes.back().biome;
}

bool BiomeMap::canTreesSpawn(float worldX, float worldZ) {
    // Get all biome influences at this position
    auto influences = getBiomeInfluences(worldX, worldZ);

    // Trees can spawn if ANY influencing biome allows them
    for (const auto& influence : influences) {
        if (influence.biome && influence.biome->trees_spawn) {
            return true;
        }
    }

    return false;
}

// ==================== Additional Property Blending Functions ====================

float BiomeMap::getBlendedVegetationDensity(float worldX, float worldZ) {
    // Get all biome influences at this position
    auto influences = getBiomeInfluences(worldX, worldZ);

    if (influences.empty()) {
        return 0.0f;  // No biomes, no vegetation
    }

    // Calculate weighted average of vegetation densities
    float blendedDensity = 0.0f;
    for (const auto& influence : influences) {
        if (influence.biome) {
            blendedDensity += influence.biome->vegetation_density * influence.weight;
        }
    }

    return blendedDensity;
}

glm::vec3 BiomeMap::getBlendedFogColor(float worldX, float worldZ) {
    // Get all biome influences at this position
    auto influences = getBiomeInfluences(worldX, worldZ);

    if (influences.empty()) {
        // Default fog color (light blue sky)
        return glm::vec3(0.5f, 0.7f, 0.9f);
    }

    // Calculate weighted average of fog colors
    // Only use custom fog colors from biomes that have them enabled
    glm::vec3 blendedColor(0.0f, 0.0f, 0.0f);
    float totalWeight = 0.0f;

    for (const auto& influence : influences) {
        if (influence.biome) {
            if (influence.biome->has_custom_fog) {
                // Use custom fog color
                blendedColor += influence.biome->fog_color * influence.weight;
                totalWeight += influence.weight;
            }
        }
    }

    // If no biomes have custom fog colors, use default
    if (totalWeight < 0.0001f) {
        return glm::vec3(0.5f, 0.7f, 0.9f);
    }

    // Normalize by the total weight of biomes with custom fog
    // (Biomes without custom fog don't contribute)
    blendedColor /= totalWeight;

    return blendedColor;
}

int BiomeMap::selectSurfaceBlock(float worldX, float worldZ) {
    // Get all biome influences at this position
    auto influences = getBiomeInfluences(worldX, worldZ);

    if (influences.empty()) {
        return 3;  // Fallback: grass block
    }

    // Weighted random selection based on biome influences
    // Uses deterministic RNG seeded by world coordinates for reproducibility

    // Create deterministic seed from world coordinates
    uint64_t seed = static_cast<uint64_t>(worldX * 73856093) ^ static_cast<uint64_t>(worldZ * 19349663);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    float roll = dist(rng);

    // Select block based on cumulative weights
    float cumulative = 0.0f;
    for (const auto& influence : influences) {
        if (influence.biome) {
            cumulative += influence.weight;
            if (roll <= cumulative) {
                return influence.biome->primary_surface_block;
            }
        }
    }

    // Fallback (should rarely happen due to floating point precision)
    return influences[0].biome->primary_surface_block;
}

int BiomeMap::selectStoneBlock(float worldX, float worldZ) {
    // Get all biome influences at this position
    auto influences = getBiomeInfluences(worldX, worldZ);

    if (influences.empty()) {
        return 1;  // Fallback: stone block
    }

    // Weighted random selection based on biome influences
    // Uses deterministic RNG seeded by world coordinates (different seed than surface)

    // Create deterministic seed from world coordinates (offset to differ from surface)
    uint64_t seed = static_cast<uint64_t>(worldX * 83492791) ^ static_cast<uint64_t>(worldZ * 28411687);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    float roll = dist(rng);

    // Select block based on cumulative weights
    float cumulative = 0.0f;
    for (const auto& influence : influences) {
        if (influence.biome) {
            cumulative += influence.weight;
            if (roll <= cumulative) {
                return influence.biome->primary_stone_block;
            }
        }
    }

    // Fallback (should rarely happen due to floating point precision)
    return influences[0].biome->primary_stone_block;
}

float BiomeMap::getBlendedTemperature(float worldX, float worldZ) {
    // Get all biome influences at this position
    auto influences = getBiomeInfluences(worldX, worldZ);

    if (influences.empty()) {
        return 50.0f;  // Fallback: temperate
    }

    // Calculate weighted average of temperatures
    float blendedTemp = 0.0f;
    for (const auto& influence : influences) {
        if (influence.biome) {
            blendedTemp += influence.biome->temperature * influence.weight;
        }
    }

    return blendedTemp;
}

float BiomeMap::getBlendedMoisture(float worldX, float worldZ) {
    // Get all biome influences at this position
    auto influences = getBiomeInfluences(worldX, worldZ);

    if (influences.empty()) {
        return 50.0f;  // Fallback: moderate moisture
    }

    // Calculate weighted average of moisture levels
    float blendedMoisture = 0.0f;
    for (const auto& influence : influences) {
        if (influence.biome) {
            blendedMoisture += influence.biome->moisture * influence.weight;
        }
    }

    return blendedMoisture;
}

// ==================== 3D BIOME INFLUENCE SYSTEM ====================

float BiomeMap::getAltitudeTemperatureModifier(float worldY) {
    /**
     * Realistic temperature lapse rate: ~6.5°C per 1000m (6.5 blocks in our scale)
     * We use a simplified version: temperature drops with altitude
     *
     * Altitude zones:
     * - Below 64 (sea level): No modifier (0)
     * - 64-100: Gradual cooling (-0 to -20)
     * - 100-150: Significant cooling (-20 to -40)
     * - 150+: Extreme cold (-40 to -60)
     */

    // No cooling below sea level
    if (worldY <= 64.0f) {
        return 0.0f;
    }

    // Temperature drops linearly with altitude above sea level
    // Every 10 blocks of elevation = -5 temperature units
    float altitudeAboveSeaLevel = worldY - 64.0f;
    float temperatureDrop = (altitudeAboveSeaLevel / 10.0f) * 5.0f;

    // Cap maximum cooling at -60 (extremely cold peaks)
    return std::min(60.0f, temperatureDrop);
}

float BiomeMap::getAltitudeInfluence(float worldY, int terrainHeight) {
    /**
     * Calculate how strongly altitude should influence biome selection
     *
     * Returns 0.0-1.0 where:
     * - 0.0 = at or below terrain height (no altitude effect)
     * - 1.0 = significantly above terrain (full altitude effect)
     *
     * Creates smooth vertical transitions
     */

    float heightAboveTerrain = worldY - static_cast<float>(terrainHeight);

    // Below terrain or at surface - no altitude influence
    if (heightAboveTerrain <= 0.0f) {
        return 0.0f;
    }

    // Add noise-based variation to prevent uniform transition lines
    float variation = m_altitudeVariation->GetNoise(worldY * 0.1f, worldY);
    float variationOffset = variation * 3.0f;  // ±3 blocks of variation

    float adjustedHeight = heightAboveTerrain + variationOffset;

    // Gradual influence increase over 20 blocks above terrain
    // This creates smooth transitions instead of sharp cutoffs
    float influence = adjustedHeight / 20.0f;

    // Clamp to [0.0, 1.0]
    return std::max(0.0f, std::min(1.0f, influence));
}

bool BiomeMap::shouldApplySnowCover(float worldX, float worldY, float worldZ) {
    /**
     * Determines if snow should cover this position
     * Considers:
     * 1. Altitude (higher = more likely)
     * 2. Base temperature from biome
     * 3. Altitude-adjusted temperature
     * 4. Noise variation for natural snow patterns
     */

    // Get base temperature from 2D biome
    float baseTemperature = getTemperatureAt(worldX, worldZ);

    // Apply altitude cooling
    float temperatureDrop = getAltitudeTemperatureModifier(worldY);
    float adjustedTemperature = baseTemperature - temperatureDrop;

    // Add snow line variation noise for natural, irregular coverage
    float snowNoise = m_snowLineNoise->GetNoise(worldX, worldZ);
    float snowLineAdjustment = snowNoise * 10.0f;  // ±10 temperature units

    float finalTemperature = adjustedTemperature + snowLineAdjustment;

    // Snow appears when temperature < 20 (cold/freezing threshold)
    // But with gradual probability for more natural appearance
    if (finalTemperature < 10.0f) {
        return true;  // Always snow when very cold
    } else if (finalTemperature < 25.0f) {
        // Gradual transition zone - probability based on temperature
        // Use position-based pseudo-random for consistency
        int seed = static_cast<int>(worldX * 73856093) ^
                   static_cast<int>(worldY * 19349663) ^
                   static_cast<int>(worldZ * 83492791);
        float random = static_cast<float>((seed & 0xFFFF)) / 65535.0f;

        // Probability decreases linearly from temp 10 to 25
        float snowProbability = 1.0f - ((finalTemperature - 10.0f) / 15.0f);
        return random < snowProbability;
    }

    return false;  // Too warm for snow
}

int BiomeMap::getAltitudeModifiedBlock(float worldX, float worldY, float worldZ, int baseSurfaceBlock) {
    /**
     * Apply altitude-based block modifications
     * Creates vertical biome transitions:
     * - Low altitude: Use base biome surface block
     * - Mid altitude: Transition to stone
     * - High altitude: Snow and ice coverage
     */

    int terrainHeight = getTerrainHeightAt(worldX, worldZ);
    float heightAboveTerrain = worldY - static_cast<float>(terrainHeight);

    // Below terrain - use base block (this handles subsurface layers)
    if (heightAboveTerrain < 0.0f) {
        return baseSurfaceBlock;
    }

    // Get altitude influence factor
    float altitudeInfluence = getAltitudeInfluence(worldY, terrainHeight);

    // === ALTITUDE ZONE 1: Surface level (heightAboveTerrain 0-2) ===
    if (heightAboveTerrain <= 2.0f) {
        // Check for snow cover at surface
        if (shouldApplySnowCover(worldX, worldY, worldZ)) {
            // Snow block for surface coverage
            return 8;  // BLOCK_SNOW (assuming block ID 8)
        }
        return baseSurfaceBlock;
    }

    // === ALTITUDE ZONE 2: Elevated terrain (heightAboveTerrain 2-15) ===
    // Gradual transition from surface block to stone
    if (heightAboveTerrain <= 15.0f && altitudeInfluence > 0.3f) {
        // Add position-based randomness for natural transition
        int seed = static_cast<int>(worldX * 73856093) ^
                   static_cast<int>(worldY * 19349663) ^
                   static_cast<int>(worldZ * 83492791);
        float random = static_cast<float>((seed & 0xFFFF)) / 65535.0f;

        // Probability of stone increases with altitude influence
        if (random < altitudeInfluence) {
            // Check if this high altitude area should have snow
            if (shouldApplySnowCover(worldX, worldY, worldZ)) {
                return 8;  // BLOCK_SNOW
            }
            return 1;  // BLOCK_STONE - exposed rock at elevation
        }
    }

    // === ALTITUDE ZONE 3: High elevation (heightAboveTerrain 15+) ===
    // Mountain peaks - primarily stone with snow coverage
    if (heightAboveTerrain > 15.0f) {
        if (shouldApplySnowCover(worldX, worldY, worldZ)) {
            return 8;  // BLOCK_SNOW - mountain peaks
        }
        return 1;  // BLOCK_STONE - exposed mountain rock
    }

    // Default fallback
    return baseSurfaceBlock;
}

std::vector<BiomeInfluence> BiomeMap::getBiomeInfluences3D(float worldX, float worldY, float worldZ) {
    /**
     * 3D Biome Influence System
     * Extends 2D biome blending with altitude-based modifications
     *
     * Strategy:
     * 1. Get base 2D biome influences
     * 2. Apply altitude modifiers based on height
     * 3. Adjust biome weights for vertical transitions
     * 4. Cache results for performance
     */

    // Create cache key (quantize to 8-block resolution for 3D)
    // Coarser than 2D cache to manage memory with extra dimension
    int quantizedX = static_cast<int>(worldX / 8.0f);
    int quantizedY = static_cast<int>(worldY / 8.0f);
    int quantizedZ = static_cast<int>(worldZ / 8.0f);
    uint64_t key = coordsToKey3D(quantizedX, quantizedY, quantizedZ);

    // Check cache first
    {
        std::shared_lock<std::shared_mutex> lock(m_influenceCache3DMutex);
        auto it = m_influenceCache3D.find(key);
        if (it != m_influenceCache3D.end()) {
            return it->second.influences;
        }
    }

    // Not in cache - compute 3D influences

    // Step 1: Get base 2D biome influences (horizontal blending)
    std::vector<BiomeInfluence> baseInfluences = getBiomeInfluences(worldX, worldZ);

    if (baseInfluences.empty()) {
        return {};  // No biomes found
    }

    // Step 2: Calculate altitude influence factor
    int terrainHeight = getTerrainHeightAt(worldX, worldZ);
    float altitudeInfluence = getAltitudeInfluence(worldY, terrainHeight);

    // Step 3: Apply altitude-based weight modifications
    // At high altitudes, cold biomes get boosted, warm biomes get reduced
    float temperatureDrop = getAltitudeTemperatureModifier(worldY);

    std::vector<BiomeInfluence> modifiedInfluences;
    modifiedInfluences.reserve(baseInfluences.size());

    float totalWeight = 0.0f;

    for (const auto& influence : baseInfluences) {
        if (!influence.biome) continue;

        // Calculate altitude-adjusted temperature for this biome
        float biomeTemp = static_cast<float>(influence.biome->temperature);
        float adjustedTemp = biomeTemp - temperatureDrop;

        // Modify weight based on altitude suitability
        float weightModifier = 1.0f;

        if (altitudeInfluence > 0.2f) {
            // At altitude: favor cold biomes, penalize warm biomes
            if (adjustedTemp < 30.0f) {
                // Cold biome - boost weight at altitude
                weightModifier = 1.0f + (altitudeInfluence * 0.5f);  // Up to +50% weight
            } else if (adjustedTemp > 60.0f) {
                // Warm biome - reduce weight at altitude
                weightModifier = 1.0f - (altitudeInfluence * 0.6f);  // Up to -60% weight
            }
        }

        float modifiedWeight = influence.weight * weightModifier;

        // Only include if weight is still significant
        if (modifiedWeight > 0.01f) {
            modifiedInfluences.emplace_back(influence.biome, modifiedWeight);
            totalWeight += modifiedWeight;
        }
    }

    // Handle edge case: all weights filtered out
    if (modifiedInfluences.empty() || totalWeight < 0.0001f) {
        // Fallback to dominant base biome
        modifiedInfluences.clear();
        modifiedInfluences.push_back(baseInfluences[0]);
        totalWeight = 1.0f;
    }

    // Step 4: Re-normalize weights to sum to 1.0
    for (auto& influence : modifiedInfluences) {
        influence.weight /= totalWeight;
    }

    // Step 5: Sort by weight (descending)
    std::sort(modifiedInfluences.begin(), modifiedInfluences.end(),
              [](const BiomeInfluence& a, const BiomeInfluence& b) {
                  return a.weight > b.weight;
              });

    // Step 6: Cache result (exclusive lock for write)
    {
        std::unique_lock<std::shared_mutex> lock(m_influenceCache3DMutex);

        // Double-check cache
        auto it = m_influenceCache3D.find(key);
        if (it != m_influenceCache3D.end()) {
            return it->second.influences;
        }

        // LRU-style eviction when cache is full
        if (m_influenceCache3D.size() >= MAX_CACHE_SIZE) {
            size_t removeCount = MAX_CACHE_SIZE / 5;  // Remove 20%
            auto removeIt = m_influenceCache3D.begin();
            for (size_t i = 0; i < removeCount && removeIt != m_influenceCache3D.end(); ++i) {
                removeIt = m_influenceCache3D.erase(removeIt);
            }
        }

        m_influenceCache3D[key] = {modifiedInfluences, altitudeInfluence};
    }

    return modifiedInfluences;
}
