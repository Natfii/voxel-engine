#include "biome_voronoi.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

BiomeVoronoi::BiomeVoronoi(int seed)
    : m_centerSpacing(400.0f)      // Centers every ~400 blocks = large biomes (2-3 chunks)
    , m_blendRadius(80.0f)         // 80 block blend zone at boundaries
    , m_seed(seed)
    , m_nextCenterID(0)
{
    // ==================== BIOME CENTER VORONOI SYSTEM ====================
    // Creates natural biome clustering using Voronoi cell principles
    //
    // Key Concept:
    // - Scatter biome "center points" across the world
    // - Each point defines the heart of a biome region
    // - Points partition space into Voronoi cells
    // - Noise distortion prevents geometric patterns
    //
    // Benefits over pure noise-based selection:
    // - Biomes have identifiable centers and coherent regions
    // - Similar biomes naturally cluster together
    // - More realistic geographic distributions
    // - Player can navigate "from forest to desert" meaningfully
    //
    // Spacing Guide:
    // - 200 blocks: Small, frequent biomes (Minecraft-like)
    // - 400 blocks: Medium biomes, good for exploration (current)
    // - 800 blocks: Large biomes, epic journeys
    // - 1600 blocks: Massive biomes, rare transitions

    // === Temperature & Moisture Noise (for center biome selection) ===
    // These determine what biome spawns at each center point
    m_temperatureNoise = std::make_unique<FastNoiseLite>(seed);
    m_temperatureNoise->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_temperatureNoise->SetFractalType(FastNoiseLite::FractalType_FBm);
    m_temperatureNoise->SetFractalOctaves(4);
    m_temperatureNoise->SetFrequency(0.0005f);  // Large-scale climate patterns

    m_moistureNoise = std::make_unique<FastNoiseLite>(seed + 100);
    m_moistureNoise->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_moistureNoise->SetFractalType(FastNoiseLite::FractalType_FBm);
    m_moistureNoise->SetFractalOctaves(4);
    m_moistureNoise->SetFrequency(0.0006f);     // Slightly different frequency for variety

    // === Distortion Noise (prevents geometric Voronoi patterns) ===
    // Warps the distance calculation to create organic boundaries
    m_distortionNoiseX = std::make_unique<FastNoiseLite>(seed + 1000);
    m_distortionNoiseX->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_distortionNoiseX->SetFractalType(FastNoiseLite::FractalType_FBm);
    m_distortionNoiseX->SetFractalOctaves(3);
    m_distortionNoiseX->SetFrequency(0.002f);   // Medium-scale distortion (~500 block features)

    m_distortionNoiseZ = std::make_unique<FastNoiseLite>(seed + 1100);
    m_distortionNoiseZ->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_distortionNoiseZ->SetFractalType(FastNoiseLite::FractalType_FBm);
    m_distortionNoiseZ->SetFractalOctaves(3);
    m_distortionNoiseZ->SetFrequency(0.002f);

    // === Jitter Noise (randomizes center positions within grid) ===
    // Prevents centers from being perfectly grid-aligned
    m_jitterNoise = std::make_unique<FastNoiseLite>(seed + 2000);
    m_jitterNoise->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_jitterNoise->SetFrequency(0.001f);        // Slow variation = consistent jitter per cell

    std::cout << "BiomeVoronoi initialized:" << std::endl;
    std::cout << "  - Center spacing: " << m_centerSpacing << " blocks" << std::endl;
    std::cout << "  - Blend radius: " << m_blendRadius << " blocks" << std::endl;
    std::cout << "  - Typical biome size: " << (m_centerSpacing * 0.8f) << "-" << (m_centerSpacing * 1.2f) << " blocks" << std::endl;
}

uint64_t BiomeVoronoi::gridToKey(int gridX, int gridZ) const {
    // Pack grid coordinates into 64-bit key
    uint32_t ux, uz;
    std::memcpy(&ux, &gridX, sizeof(uint32_t));
    std::memcpy(&uz, &gridZ, sizeof(uint32_t));
    return (static_cast<uint64_t>(ux) << 32) | static_cast<uint64_t>(uz);
}

glm::vec2 BiomeVoronoi::worldToGrid(float worldX, float worldZ) const {
    return glm::vec2(worldX / m_centerSpacing, worldZ / m_centerSpacing);
}

const Biome* BiomeVoronoi::selectBiomeForCenter(float temperature, float moisture) const {
    auto& biomeRegistry = BiomeRegistry::getInstance();

    if (biomeRegistry.getBiomeCount() == 0) {
        std::cerr << "Warning: No biomes loaded for center selection!" << std::endl;
        return nullptr;
    }

    // Select biome based on temperature and moisture at center point
    // Use simple closest-match algorithm for deterministic center selection
    const Biome* bestBiome = nullptr;
    float bestDistance = std::numeric_limits<float>::max();

    for (int i = 0; i < biomeRegistry.getBiomeCount(); i++) {
        const Biome* biome = biomeRegistry.getBiomeByIndex(i);
        if (!biome) continue;

        // Calculate distance in climate space
        float tempDist = std::abs(temperature - biome->temperature);
        float moistDist = std::abs(moisture - biome->moisture);
        float totalDist = std::sqrt(tempDist * tempDist + moistDist * moistDist);

        // Weight by rarity (higher rarity weight = more common)
        float weightedDist = totalDist / (biome->biome_rarity_weight / 50.0f);

        if (weightedDist < bestDistance) {
            bestDistance = weightedDist;
            bestBiome = biome;
        }
    }

    return bestBiome;
}

void BiomeVoronoi::generateCenterForCell(int gridX, int gridZ) {
    uint64_t key = gridToKey(gridX, gridZ);

    // Check if already generated
    if (m_centerCache.find(key) != m_centerCache.end()) {
        return;
    }

    // Calculate base position for this grid cell
    float baseX = gridX * m_centerSpacing;
    float baseZ = gridZ * m_centerSpacing;

    // Add jitter to prevent perfect grid alignment
    // Jitter range: ±30% of spacing
    float jitterRange = m_centerSpacing * 0.3f;
    float jitterX = m_jitterNoise->GetNoise(baseX, baseZ) * jitterRange;
    float jitterZ = m_jitterNoise->GetNoise(baseX + 1000.0f, baseZ + 1000.0f) * jitterRange;

    glm::vec2 centerPos(baseX + jitterX, baseZ + jitterZ);

    // Sample climate at center position
    float temperature = m_temperatureNoise->GetNoise(centerPos.x, centerPos.y);
    temperature = (temperature + 1.0f) * 50.0f;  // Map [-1, 1] to [0, 100]

    float moisture = m_moistureNoise->GetNoise(centerPos.x, centerPos.y);
    moisture = (moisture + 1.0f) * 50.0f;  // Map [-1, 1] to [0, 100]

    // Select biome for this center
    const Biome* biome = selectBiomeForCenter(temperature, moisture);

    if (!biome) {
        // Fallback to first biome if selection fails
        auto& biomeRegistry = BiomeRegistry::getInstance();
        if (biomeRegistry.getBiomeCount() > 0) {
            biome = biomeRegistry.getBiomeByIndex(0);
        }
    }

    // Create center point
    BiomeCenter center(centerPos, biome, temperature, moisture, m_nextCenterID++);

    // Cache it
    m_centerCache[key] = {center};
}

std::vector<BiomeCenter> BiomeVoronoi::getCentersInRegion(float minX, float maxX, float minZ, float maxZ) {
    std::vector<BiomeCenter> centers;

    // Convert world coordinates to grid coordinates
    int minGridX = static_cast<int>(std::floor(minX / m_centerSpacing));
    int maxGridX = static_cast<int>(std::ceil(maxX / m_centerSpacing));
    int minGridZ = static_cast<int>(std::floor(minZ / m_centerSpacing));
    int maxGridZ = static_cast<int>(std::ceil(maxZ / m_centerSpacing));

    // Generate and collect centers in region
    for (int gridZ = minGridZ; gridZ <= maxGridZ; gridZ++) {
        for (int gridX = minGridX; gridX <= maxGridX; gridX++) {
            generateCenterForCell(gridX, gridZ);
            uint64_t key = gridToKey(gridX, gridZ);

            auto it = m_centerCache.find(key);
            if (it != m_centerCache.end()) {
                for (const auto& center : it->second) {
                    // Only include centers actually in the region
                    if (center.position.x >= minX && center.position.x <= maxX &&
                        center.position.y >= minZ && center.position.y <= maxZ) {
                        centers.push_back(center);
                    }
                }
            }
        }
    }

    return centers;
}

glm::vec2 BiomeVoronoi::getDistortedPosition(float worldX, float worldZ) const {
    // Apply noise-based distortion to position
    // This warps the distance calculation and prevents geometric Voronoi cells
    float distortionStrength = m_centerSpacing * 0.15f;  // 15% of spacing

    float offsetX = m_distortionNoiseX->GetNoise(worldX, worldZ) * distortionStrength;
    float offsetZ = m_distortionNoiseZ->GetNoise(worldX, worldZ) * distortionStrength;

    return glm::vec2(worldX + offsetX, worldZ + offsetZ);
}

std::vector<std::pair<BiomeCenter, float>> BiomeVoronoi::findNearestCenters(float worldX, float worldZ, int maxCenters) {
    // Apply distortion to query position for organic boundaries
    glm::vec2 distortedPos = getDistortedPosition(worldX, worldZ);

    // Search a region around the query point
    // Search radius: 2x spacing to ensure we find all nearby centers
    float searchRadius = m_centerSpacing * 2.0f;
    std::vector<BiomeCenter> nearbyCenters = getCentersInRegion(
        distortedPos.x - searchRadius, distortedPos.x + searchRadius,
        distortedPos.y - searchRadius, distortedPos.y + searchRadius
    );

    // Calculate distances to all nearby centers
    std::vector<std::pair<BiomeCenter, float>> centerDistances;
    centerDistances.reserve(nearbyCenters.size());

    for (const auto& center : nearbyCenters) {
        float dx = distortedPos.x - center.position.x;
        float dz = distortedPos.y - center.position.y;
        float distance = std::sqrt(dx * dx + dz * dz);

        centerDistances.emplace_back(center, distance);
    }

    // Sort by distance (ascending)
    std::sort(centerDistances.begin(), centerDistances.end(),
              [](const auto& a, const auto& b) {
                  return a.second < b.second;
              });

    // Return only the N nearest centers
    if (static_cast<int>(centerDistances.size()) > maxCenters) {
        centerDistances.resize(maxCenters);
    }

    return centerDistances;
}

float BiomeVoronoi::calculateVoronoiWeight(float distance, float minDistance, float blendRadius) const {
    // ==================== VORONOI WEIGHT CALCULATION ====================
    // Creates smooth transitions between Voronoi cells
    //
    // Strategy:
    // - Points very close to a center get high weight (approaching 1.0)
    // - Points far from all centers get low weight
    // - Smooth falloff in the blend zone creates natural transitions
    //
    // The blend zone is where multiple centers compete for influence
    // Outside the blend zone, the nearest center dominates

    if (distance <= 0.0f) {
        return 1.0f;  // Right at the center
    }

    // Calculate how far into the blend zone we are
    // blendFactor = 0.0 at minDistance, 1.0 at minDistance + blendRadius
    float blendFactor = (distance - minDistance) / blendRadius;
    blendFactor = std::max(0.0f, std::min(1.0f, blendFactor));

    // Use smooth exponential falloff
    // Weight = exp(-k * blendFactor^2)
    // This creates smooth, natural-looking transitions
    float falloffStrength = 3.0f;  // Higher = sharper transitions
    float weight = std::exp(-falloffStrength * blendFactor * blendFactor);

    return weight;
}
