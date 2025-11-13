#pragma once

#include "FastNoiseLite.h"
#include "biome_system.h"
#include <memory>
#include <mutex>
#include <unordered_map>
#include <glm/glm.hpp>

/**
 * Generates and caches biome assignments for world coordinates
 * Uses temperature and moisture noise to select appropriate biomes
 */
class BiomeMap {
public:
    BiomeMap(int seed);
    ~BiomeMap() = default;

    /**
     * Get the biome at a specific world position (2D)
     * Uses world coordinates to ensure seamless generation across chunk boundaries
     *
     * @param worldX X coordinate in world space
     * @param worldZ Z coordinate in world space
     * @return Pointer to the biome at this location
     */
    const Biome* getBiomeAt(float worldX, float worldZ);

    /**
     * Get temperature value at a world position (0-100)
     */
    float getTemperatureAt(float worldX, float worldZ);

    /**
     * Get moisture value at a world position (0-100)
     */
    float getMoistureAt(float worldX, float worldZ);

    /**
     * Get the base terrain height at a world position
     * Uses biome's age property to determine roughness
     */
    int getTerrainHeightAt(float worldX, float worldZ);

    /**
     * Get cave density at a 3D world position
     * Returns value 0.0-1.0 where higher = more solid (not cave)
     * Values < 0.45 = air (cave), >= 0.45 = solid
     */
    float getCaveDensityAt(float worldX, float worldY, float worldZ);

    /**
     * Check if a position is inside an underground biome chamber
     */
    bool isUndergroundBiomeAt(float worldX, float worldY, float worldZ);

private:
    // Noise generators
    std::unique_ptr<FastNoiseLite> m_temperatureNoise;
    std::unique_ptr<FastNoiseLite> m_moistureNoise;
    std::unique_ptr<FastNoiseLite> m_terrainNoise;
    std::unique_ptr<FastNoiseLite> m_caveNoise;              // Chamber-style caves
    std::unique_ptr<FastNoiseLite> m_caveTunnelNoise;        // Long winding tunnels
    std::unique_ptr<FastNoiseLite> m_undergroundChamberNoise;

    // Secondary noise for variation
    std::unique_ptr<FastNoiseLite> m_temperatureVariation;
    std::unique_ptr<FastNoiseLite> m_moistureVariation;

    // Thread safety
    mutable std::mutex m_noiseMutex;

    // Cached biome lookups (for performance)
    struct BiomeCell {
        const Biome* biome;
        float temperature;
        float moisture;
    };
    static constexpr size_t MAX_CACHE_SIZE = 100000;  // ~3MB max (prevents memory leak)
    std::unordered_map<uint64_t, BiomeCell> m_biomeCache;
    mutable std::mutex m_cacheMutex;

    // Helper functions
    uint64_t coordsToKey(int x, int z) const;
    const Biome* selectBiome(float temperature, float moisture);
    float mapNoiseTo01(float noise);  // Maps [-1, 1] to [0, 1]
    float mapNoiseToRange(float noise, float min, float max);
};
