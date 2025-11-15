#pragma once

#include "FastNoiseLite.h"
#include "biome_system.h"
#include "biome_transition_config.h"
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

/**
 * Represents a biome's influence/weight at a specific position
 * Used for smooth biome blending and transitions
 */
struct BiomeInfluence {
    const Biome* biome;  // Pointer to the biome
    float weight;        // Normalized influence weight (0.0 to 1.0)

    BiomeInfluence(const Biome* b, float w) : biome(b), weight(w) {}
};

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
     * Get weighted biome influences at a world position
     * Returns all biomes that have influence at this position with their normalized weights
     * Weights are guaranteed to sum to 1.0
     *
     * This is the core of the biome blending system. Multiple biomes can influence
     * a single position, with smooth distance-based falloff.
     *
     * @param worldX X coordinate in world space
     * @param worldZ Z coordinate in world space
     * @return Vector of biome influences (typically 1-4 biomes)
     */
    std::vector<BiomeInfluence> getBiomeInfluences(float worldX, float worldZ);

    /**
     * Get temperature value at a world position (0-100)
     */
    float getTemperatureAt(float worldX, float worldZ);

    /**
     * Get moisture value at a world position (0-100)
     */
    float getMoistureAt(float worldX, float worldZ);

    /**
     * Get weirdness value at a world position (0-100)
     * Controls how "strange" or varied the biome selection can be
     * Similar to Minecraft 1.18+ continentalness/weirdness parameter
     */
    float getWeirdnessAt(float worldX, float worldZ);

    /**
     * Get erosion value at a world position (0-100)
     * Influences terrain roughness and biome transitions
     */
    float getErosionAt(float worldX, float worldZ);

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

    /**
     * Get blended tree density at a world position
     * Returns weighted average tree density based on biome influences
     *
     * @param worldX X coordinate in world space
     * @param worldZ Z coordinate in world space
     * @return Blended tree density (0-100)
     */
    float getBlendedTreeDensity(float worldX, float worldZ);

    /**
     * Select a biome for tree placement using weighted random selection
     * Uses biome influences to probabilistically choose which biome's trees to use
     *
     * @param worldX X coordinate in world space
     * @param worldZ Z coordinate in world space
     * @return Pointer to selected biome for tree placement (nullptr if no valid biome)
     */
    const Biome* selectTreeBiome(float worldX, float worldZ);

    /**
     * Check if trees should spawn at a position based on blended biome rules
     *
     * @param worldX X coordinate in world space
     * @param worldZ Z coordinate in world space
     * @return true if any influencing biome allows tree spawning
     */
    bool canTreesSpawn(float worldX, float worldZ);

    /**
     * Set the transition profile for biome blending
     * Allows runtime configuration of transition behavior
     *
     * @param profile The transition profile to use
     */
    void setTransitionProfile(const BiomeTransition::TransitionProfile& profile);

    /**
     * Get the current transition profile
     * @return Reference to the current transition profile
     */
    const BiomeTransition::TransitionProfile& getTransitionProfile() const { return m_transitionProfile; }

private:
    // Biome transition configuration
    // Uses configurable profile system for flexible transition tuning
    BiomeTransition::TransitionProfile m_transitionProfile;

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

    // Tertiary noise layers for biome variety (Minecraft 1.18+ style)
    std::unique_ptr<FastNoiseLite> m_weirdnessNoise;         // Large-scale weird biome patterns
    std::unique_ptr<FastNoiseLite> m_weirdnessDetail;        // Local weirdness variation
    std::unique_ptr<FastNoiseLite> m_erosionNoise;           // Erosion patterns for terrain
    std::unique_ptr<FastNoiseLite> m_erosionDetail;          // Local erosion variation

    // Cached biome lookups (for performance)
    struct BiomeCell {
        const Biome* biome;
        float temperature;
        float moisture;
        float weirdness;
        float erosion;
    };
    static constexpr size_t MAX_CACHE_SIZE = 100000;  // ~3MB max (prevents memory leak)
    std::unordered_map<uint64_t, BiomeCell> m_biomeCache;
    mutable std::shared_mutex m_cacheMutex;  // Shared mutex: parallel reads, exclusive writes

    // Biome influence cache (for blending performance)
    struct InfluenceCache {
        std::vector<BiomeInfluence> influences;
    };
    std::unordered_map<uint64_t, InfluenceCache> m_influenceCache;
    mutable std::shared_mutex m_influenceCacheMutex;

    // WEEK 1 OPTIMIZATION: Cache expensive per-block lookups
    // Terrain height cache (2D: worldX, worldZ) - quantized to 2-block resolution
    std::unordered_map<uint64_t, int> m_terrainHeightCache;
    mutable std::shared_mutex m_terrainCacheMutex;

    // Cave density cache (3D: worldX, worldY, worldZ) - quantized to 2-block resolution
    std::unordered_map<uint64_t, float> m_caveDensityCache;
    mutable std::shared_mutex m_caveCacheMutex;

    // Random number generator for feature blending
    mutable std::mt19937 m_featureRng;
    mutable std::mutex m_rngMutex;  // Protects RNG access in multi-threaded contexts

    // Helper functions
    uint64_t coordsToKey(int x, int z) const;
    uint64_t coordsToKey3D(int x, int y, int z) const;
    const Biome* selectBiome(float temperature, float moisture, float weirdness, float erosion);
    float mapNoiseTo01(float noise);  // Maps [-1, 1] to [0, 1]
    float mapNoiseToRange(float noise, float min, float max);

    /**
     * Calculate influence weight for a biome based on distance
     * Uses smooth falloff function with linear inner zone and exponential outer zone
     *
     * @param distance Distance in temperature/moisture space
     * @param rarityWeight Biome's rarity weight (0-100)
     * @return Influence weight (0.0 to 1.0+, before normalization)
     */
    float calculateInfluenceWeight(float distance, float rarityWeight) const;
};
