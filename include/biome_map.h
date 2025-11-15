#pragma once

#include "FastNoiseLite.h"
#include "biome_system.h"
#include "biome_transition_config.h"
#include "biome_noise_config.h"
#include "biome_voronoi.h"
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>
#include <random>
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
    BiomeMap(int seed, const BiomeNoise::BiomeNoiseConfig& config);
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
     * Get blended vegetation density at a world position
     * Returns weighted average vegetation density based on biome influences
     *
     * @param worldX X coordinate in world space
     * @param worldZ Z coordinate in world space
     * @return Blended vegetation density (0-100)
     */
    float getBlendedVegetationDensity(float worldX, float worldZ);

    /**
     * Get blended fog color at a world position
     * Returns weighted average of biome fog colors
     * Uses custom fog colors only from biomes that have them enabled
     *
     * @param worldX X coordinate in world space
     * @param worldZ Z coordinate in world space
     * @return Blended fog color (RGB vec3)
     */
    glm::vec3 getBlendedFogColor(float worldX, float worldZ);

    /**
     * Select a surface block using weighted random selection
     * Uses biome influences to probabilistically choose which biome's surface block to use
     * Provides natural-looking gradual transitions (e.g., sand -> grass)
     *
     * @param worldX X coordinate in world space
     * @param worldZ Z coordinate in world space
     * @return Block ID for surface block
     */
    int selectSurfaceBlock(float worldX, float worldZ);

    /**
     * Select a stone block using weighted random selection
     * Uses biome influences to probabilistically choose which biome's stone block to use
     *
     * @param worldX X coordinate in world space
     * @param worldZ Z coordinate in world space
     * @return Block ID for stone block
     */
    int selectStoneBlock(float worldX, float worldZ);

    /**
     * Get blended temperature value at a position
     * Returns weighted average temperature based on biome influences
     *
     * @param worldX X coordinate in world space
     * @param worldZ Z coordinate in world space
     * @return Blended temperature (0-100)
     */
    float getBlendedTemperature(float worldX, float worldZ);

    /**
     * Get blended moisture value at a position
     * Returns weighted average moisture based on biome influences
     *
     * @param worldX X coordinate in world space
     * @param worldZ Z coordinate in world space
     * @return Blended moisture (0-100)
     */
    float getBlendedMoisture(float worldX, float worldZ);

    // ==================== 3D BIOME INFLUENCE SYSTEM ====================

    /**
     * Get weighted biome influences at a 3D world position
     * Extends the 2D biome system with altitude-based modifications
     * Enables vertical biome transitions (e.g., snow on mountain peaks)
     *
     * @param worldX X coordinate in world space
     * @param worldY Y coordinate (altitude) in world space
     * @param worldZ Z coordinate in world space
     * @return Vector of biome influences with altitude modifiers applied
     */
    std::vector<BiomeInfluence> getBiomeInfluences3D(float worldX, float worldY, float worldZ);

    /**
     * Calculate altitude influence factor
     * Returns how much altitude should modify biome selection (0.0 to 1.0)
     *
     * @param worldY Current Y coordinate
     * @param terrainHeight Base terrain height at this XZ position
     * @return Altitude factor (0.0 = no effect, 1.0 = maximum effect)
     */
    float getAltitudeInfluence(float worldY, int terrainHeight);

    /**
     * Get the appropriate surface block for a 3D position with altitude blending
     * Applies vertical transitions (e.g., grass -> stone -> snow with altitude)
     *
     * @param worldX X coordinate in world space
     * @param worldY Y coordinate in world space
     * @param worldZ Z coordinate in world space
     * @param baseSurfaceBlock The base surface block from 2D biome
     * @return Block ID for this altitude-modified position
     */
    int getAltitudeModifiedBlock(float worldX, float worldY, float worldZ, int baseSurfaceBlock);

    /**
     * Check if snow should be applied at this altitude
     * Considers both altitude and biome temperature
     *
     * @param worldX X coordinate in world space
     * @param worldY Y coordinate in world space
     * @param worldZ Z coordinate in world space
     * @return true if snow cover should be applied
     */
    bool shouldApplySnowCover(float worldX, float worldY, float worldZ);

    /**
     * Get altitude-based temperature modifier
     * Temperature decreases with altitude (realistic lapse rate)
     *
     * @param worldY Y coordinate in world space
     * @return Temperature reduction (0-100 scale)
     */
    float getAltitudeTemperatureModifier(float worldY);

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

    // ==================== Voronoi Center System ====================

    /**
     * Enable or disable Voronoi-based biome clustering
     * When enabled, biomes form around center points with natural boundaries
     * When disabled, uses traditional noise-based blending
     *
     * @param enable true to use Voronoi centers, false for traditional noise
     */
    void setVoronoiMode(bool enable) { m_useVoronoiMode = enable; }

    /**
     * Check if Voronoi mode is currently enabled
     * @return true if using Voronoi center-based biome selection
     */
    bool isVoronoiMode() const { return m_useVoronoiMode; }

    /**
     * Get the underlying Voronoi system for configuration
     * @return Pointer to BiomeVoronoi, or nullptr if not initialized
     */
    BiomeVoronoi* getVoronoi() { return m_voronoi.get(); }

    // ==================== Multi-Layer Noise Configuration ====================

    /**
     * Reconfigure the noise system with a new configuration
     * Clears all caches and reinitializes noise generators
     *
     * @param config The new noise configuration to use
     */
    void setNoiseConfig(const BiomeNoise::BiomeNoiseConfig& config);

    /**
     * Get the current noise configuration
     * @return Reference to the current noise configuration
     */
    const BiomeNoise::BiomeNoiseConfig& getNoiseConfig() const { return m_noiseConfig; }

    /**
     * Update a specific dimension's configuration
     * Allows fine-tuning individual dimensions without affecting others
     *
     * @param dimension Which dimension to update (0=temp, 1=moisture, 2=weirdness, 3=erosion)
     * @param config The new dimension configuration
     */
    void setDimensionConfig(int dimension, const BiomeNoise::DimensionConfig& config);

    /**
     * Update a single noise layer's parameters
     * For precise control over individual noise layers
     *
     * @param dimension Which dimension (0=temp, 1=moisture, 2=weirdness, 3=erosion)
     * @param isBaseLayer true for base layer, false for detail layer
     * @param layerConfig The new layer configuration
     */
    void setLayerConfig(int dimension, bool isBaseLayer, const BiomeNoise::NoiseLayerConfig& layerConfig);

    /**
     * Apply a preset configuration
     * Convenience method for switching between scale presets
     *
     * @param presetName "continental", "regional", "local", or "compact"
     */
    void applyPreset(const std::string& presetName);

private:
    // Biome transition configuration
    // Uses configurable profile system for flexible transition tuning
    BiomeTransition::TransitionProfile m_transitionProfile;

    // Multi-layer noise configuration
    BiomeNoise::BiomeNoiseConfig m_noiseConfig;
    int m_seed;

    // Biome Voronoi center system (NEW: Agent 24)
    std::unique_ptr<BiomeVoronoi> m_voronoi;
    bool m_useVoronoiMode;  // Toggle between Voronoi and traditional noise-based selection

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

    // 3D biome influence system - altitude-based noise
    std::unique_ptr<FastNoiseLite> m_altitudeVariation;      // Adds variation to altitude transitions
    std::unique_ptr<FastNoiseLite> m_snowLineNoise;          // Controls snow line height variation

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

    // 3D biome influence cache (for altitude-based blending)
    struct InfluenceCache3D {
        std::vector<BiomeInfluence> influences;
        float altitudeInfluence;
    };
    std::unordered_map<uint64_t, InfluenceCache3D> m_influenceCache3D;
    mutable std::shared_mutex m_influenceCache3DMutex;

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

    /**
     * Generate per-biome noise with custom octaves, lacunarity, and gain
     * Allows each biome to have unique terrain roughness characteristics
     *
     * @param x World X coordinate
     * @param z World Z coordinate
     * @param octaves Number of noise octaves (more = more detail)
     * @param baseFrequency Base frequency for noise
     * @param lacunarity Frequency multiplier per octave
     * @param gain Amplitude decay per octave
     * @return Noise value in range [-1, 1]
     */
    float generatePerBiomeNoise(float x, float z, int octaves,
                               float baseFrequency, float lacunarity, float gain) const;

    // Noise configuration helpers
    void initializeNoiseGenerators();
    void applyLayerConfig(FastNoiseLite* noise, const BiomeNoise::NoiseLayerConfig& config);
    void clearAllCaches();
};
