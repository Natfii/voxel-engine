#pragma once

#include "FastNoiseLite.h"
#include "biome_system.h"
#include <memory>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>

/**
 * Represents a biome center point in the world
 * Each center serves as an anchor for a Voronoi cell
 */
struct BiomeCenter {
    glm::vec2 position;        // World position (X, Z)
    const Biome* biome;        // The biome at this center
    float temperature;         // Temperature at center
    float moisture;            // Moisture at center
    int id;                    // Unique identifier for this center

    BiomeCenter(const glm::vec2& pos, const Biome* b, float temp, float moist, int centerID)
        : position(pos), biome(b), temperature(temp), moisture(moist), id(centerID) {}
};

/**
 * Implements Voronoi-based biome clustering system
 * Creates natural biome distributions with clear centers and boundaries
 *
 * Design Philosophy:
 * - Biome centers are scattered across the world using Poisson-disc-like distribution
 * - Each point in the world belongs to the "cell" of its nearest center(s)
 * - Distance-based weighting creates smooth transitions at boundaries
 * - Noise distortion prevents perfectly geometric Voronoi patterns
 *
 * This creates more realistic biome clustering compared to pure noise-based selection:
 * - Biomes have identifiable "heart" regions (centers)
 * - Similar biomes can cluster together naturally
 * - Transitions occur at natural boundaries between centers
 */
class BiomeVoronoi {
public:
    BiomeVoronoi(int seed);
    ~BiomeVoronoi() = default;

    /**
     * Get biome center points within a region
     * Centers are generated on-demand and cached for consistency
     *
     * @param minX Minimum X coordinate
     * @param maxX Maximum X coordinate
     * @param minZ Minimum Z coordinate
     * @param maxZ Maximum Z coordinate
     * @return Vector of biome centers in this region
     */
    std::vector<BiomeCenter> getCentersInRegion(float minX, float maxX, float minZ, float maxZ);

    /**
     * Find the N nearest biome centers to a world position
     * Uses efficient spatial lookup with noise-based distortion
     *
     * @param worldX X coordinate in world space
     * @param worldZ Z coordinate in world space
     * @param maxCenters Maximum number of centers to return (typically 3-5)
     * @return Vector of nearest centers with distances
     */
    std::vector<std::pair<BiomeCenter, float>> findNearestCenters(float worldX, float worldZ, int maxCenters = 4);

    /**
     * Calculate Voronoi cell weight for a center based on distance
     * Uses smooth falloff to create natural transitions between cells
     *
     * @param distance Distance from point to center
     * @param minDistance Distance to closest center
     * @param blendRadius How far to blend between cells
     * @return Weight value (0.0 to 1.0+, before normalization)
     */
    float calculateVoronoiWeight(float distance, float minDistance, float blendRadius) const;

    /**
     * Get distorted position using noise
     * Prevents perfectly geometric Voronoi cells
     *
     * @param worldX X coordinate
     * @param worldZ Z coordinate
     * @return Distorted position
     */
    glm::vec2 getDistortedPosition(float worldX, float worldZ) const;

    /**
     * Set the spacing between biome centers
     * Larger spacing = larger biomes
     *
     * @param spacing Distance between centers (default: 400 blocks)
     */
    void setCenterSpacing(float spacing) { m_centerSpacing = spacing; }

    /**
     * Get current center spacing
     */
    float getCenterSpacing() const { return m_centerSpacing; }

    /**
     * Set blend radius for Voronoi transitions
     * Larger radius = smoother, wider transitions
     *
     * @param radius Blend radius in blocks (default: 80 blocks)
     */
    void setBlendRadius(float radius) { m_blendRadius = radius; }

    /**
     * Get current blend radius
     */
    float getBlendRadius() const { return m_blendRadius; }

private:
    // Configuration
    float m_centerSpacing;     // Distance between biome centers (blocks)
    float m_blendRadius;       // Radius for blending between Voronoi cells (blocks)
    int m_seed;                // World seed

    // Noise generators for center selection and distortion
    std::unique_ptr<FastNoiseLite> m_temperatureNoise;    // For biome selection at centers
    std::unique_ptr<FastNoiseLite> m_moistureNoise;       // For biome selection at centers
    std::unique_ptr<FastNoiseLite> m_distortionNoiseX;    // Prevents geometric patterns
    std::unique_ptr<FastNoiseLite> m_distortionNoiseZ;    // Prevents geometric patterns
    std::unique_ptr<FastNoiseLite> m_jitterNoise;         // Randomizes center positions

    // Center point cache
    // Key: grid cell coordinate (x, z) packed into 64-bit int
    // Value: vector of centers in that grid cell
    std::unordered_map<uint64_t, std::vector<BiomeCenter>> m_centerCache;

    // Helper functions
    uint64_t gridToKey(int gridX, int gridZ) const;
    void generateCenterForCell(int gridX, int gridZ);
    const Biome* selectBiomeForCenter(float temperature, float moisture) const;
    glm::vec2 worldToGrid(float worldX, float worldZ) const;

    // Counter for unique center IDs
    int m_nextCenterID;
};
