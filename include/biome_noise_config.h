#pragma once

#include "FastNoiseLite.h"
#include <string>

/**
 * Multi-Layer Biome Noise Configuration System
 *
 * This system provides independent control over each noise layer used in biome selection.
 * Each biome dimension (temperature, moisture, weirdness, erosion) has two noise layers:
 * - Base layer: Large-scale patterns (continental/regional)
 * - Detail layer: Local variations within regions
 *
 * Design Philosophy:
 * - Frequency: Controls scale (lower = larger features)
 * - Octaves: Controls detail level (more = more detail)
 * - Lacunarity: Controls detail frequency multiplication (higher = more varied octaves)
 * - Gain: Controls octave amplitude (higher = stronger detail layers)
 * - Blend: Controls base/detail mixing ratio
 */

namespace BiomeNoise {

/**
 * Configuration for a single noise layer
 */
struct NoiseLayerConfig {
    // Noise parameters
    FastNoiseLite::NoiseType noiseType = FastNoiseLite::NoiseType_OpenSimplex2;
    FastNoiseLite::FractalType fractalType = FastNoiseLite::FractalType_FBm;
    float frequency = 0.001f;        // Lower = wider features (0.0001-0.01 typical)
    int octaves = 4;                 // More = more detail (1-8 typical)
    float lacunarity = 2.0f;         // Detail frequency multiplier (1.5-3.0 typical)
    float gain = 0.5f;               // Octave amplitude (0.3-0.7 typical)

    // Layer metadata
    std::string name = "Unnamed";
    std::string description = "";

    NoiseLayerConfig() = default;

    NoiseLayerConfig(const std::string& layerName,
                     FastNoiseLite::NoiseType type,
                     float freq, int oct = 4,
                     float lac = 2.0f, float g = 0.5f)
        : noiseType(type), frequency(freq), octaves(oct),
          lacunarity(lac), gain(g), name(layerName) {}
};

/**
 * Configuration for a complete biome dimension (base + detail layers)
 */
struct DimensionConfig {
    NoiseLayerConfig baseLayer;      // Large-scale patterns
    NoiseLayerConfig detailLayer;    // Local variations
    float blendRatio = 0.7f;         // 0.0-1.0: 0=all detail, 1=all base

    std::string dimensionName = "Unnamed";
    std::string description = "";

    DimensionConfig() = default;
};

/**
 * Complete biome noise configuration
 * Contains all parameters for the 4-dimensional biome selection system
 */
struct BiomeNoiseConfig {
    // === BIOME DIMENSIONS ===
    DimensionConfig temperature;     // Cold to hot gradient
    DimensionConfig moisture;        // Dry to wet gradient
    DimensionConfig weirdness;       // Normal to unusual biome combinations
    DimensionConfig erosion;         // Smooth to rough terrain

    // === SELECTION PARAMETERS ===
    float primaryTolerance = 20.0f;      // Temperature/moisture matching tolerance
    float weirdnessInfluence = 0.3f;     // Weirdness effect on selection (0.0-1.0)
    float erosionInfluence = 0.15f;      // Erosion effect on selection (0.0-1.0)

    // Metadata
    std::string configName = "Custom";
    std::string description = "";

    BiomeNoiseConfig() = default;
};

// ==================== PRESET CONFIGURATIONS ====================

/**
 * CONTINENTAL SCALE (Default)
 * - Extra large biomes spanning 2000-3000+ blocks
 * - Smooth, gradual transitions
 * - Realistic continent-like climate zones
 */
inline BiomeNoiseConfig createContinentalConfig() {
    BiomeNoiseConfig config;
    config.configName = "Continental Scale";
    config.description = "Extra large biomes with smooth, realistic transitions";

    // Temperature: Very wide cold/hot zones
    config.temperature.dimensionName = "Temperature";
    config.temperature.baseLayer = NoiseLayerConfig(
        "Temperature Base",
        FastNoiseLite::NoiseType_OpenSimplex2,
        0.0003f,  // ~3333 block features
        5, 2.2f, 0.55f
    );
    config.temperature.detailLayer = NoiseLayerConfig(
        "Temperature Detail",
        FastNoiseLite::NoiseType_OpenSimplex2,
        0.003f,   // ~333 block features
        3, 2.0f, 0.5f
    );
    config.temperature.blendRatio = 0.7f;  // 70% base, 30% detail

    // Moisture: Very wide dry/wet zones
    config.moisture.dimensionName = "Moisture";
    config.moisture.baseLayer = NoiseLayerConfig(
        "Moisture Base",
        FastNoiseLite::NoiseType_OpenSimplex2,
        0.0004f,  // ~2500 block features
        5, 2.2f, 0.55f
    );
    config.moisture.detailLayer = NoiseLayerConfig(
        "Moisture Detail",
        FastNoiseLite::NoiseType_OpenSimplex2,
        0.0035f,  // ~285 block features
        3, 2.0f, 0.5f
    );
    config.moisture.blendRatio = 0.7f;

    // Weirdness: Continental-scale unusual patterns
    config.weirdness.dimensionName = "Weirdness";
    config.weirdness.baseLayer = NoiseLayerConfig(
        "Weirdness Base",
        FastNoiseLite::NoiseType_OpenSimplex2,
        0.0003f,  // ~3333 block features
        4, 2.5f, 0.6f
    );
    config.weirdness.detailLayer = NoiseLayerConfig(
        "Weirdness Detail",
        FastNoiseLite::NoiseType_Perlin,  // Smoother detail
        0.002f,   // ~500 block features
        2, 2.0f, 0.5f
    );
    config.weirdness.blendRatio = 0.65f;  // More detail for variety

    // Erosion: Wide erosion patterns
    config.erosion.dimensionName = "Erosion";
    config.erosion.baseLayer = NoiseLayerConfig(
        "Erosion Base",
        FastNoiseLite::NoiseType_OpenSimplex2,
        0.0004f,  // ~2500 block features
        4, 2.3f, 0.5f
    );
    config.erosion.baseLayer.fractalType = FastNoiseLite::FractalType_Ridged;  // Ridged for erosion
    config.erosion.detailLayer = NoiseLayerConfig(
        "Erosion Detail",
        FastNoiseLite::NoiseType_OpenSimplex2,
        0.0025f,  // ~400 block features
        3, 2.0f, 0.5f
    );
    config.erosion.blendRatio = 0.6f;  // More detail influence

    return config;
}

/**
 * REGIONAL SCALE
 * - Large biomes spanning 1000-2000 blocks
 * - Balanced transitions
 * - Good variety without excessive scale
 */
inline BiomeNoiseConfig createRegionalConfig() {
    BiomeNoiseConfig config;
    config.configName = "Regional Scale";
    config.description = "Large biomes with balanced variety and transitions";

    // Temperature
    config.temperature.dimensionName = "Temperature";
    config.temperature.baseLayer = NoiseLayerConfig(
        "Temperature Base",
        FastNoiseLite::NoiseType_OpenSimplex2,
        0.0006f,  // ~1666 block features
        5, 2.2f, 0.55f
    );
    config.temperature.detailLayer = NoiseLayerConfig(
        "Temperature Detail",
        FastNoiseLite::NoiseType_OpenSimplex2,
        0.005f,   // ~200 block features
        3, 2.0f, 0.5f
    );
    config.temperature.blendRatio = 0.65f;

    // Moisture
    config.moisture.dimensionName = "Moisture";
    config.moisture.baseLayer = NoiseLayerConfig(
        "Moisture Base",
        FastNoiseLite::NoiseType_OpenSimplex2,
        0.0007f,  // ~1428 block features
        5, 2.2f, 0.55f
    );
    config.moisture.detailLayer = NoiseLayerConfig(
        "Moisture Detail",
        FastNoiseLite::NoiseType_OpenSimplex2,
        0.006f,   // ~166 block features
        3, 2.0f, 0.5f
    );
    config.moisture.blendRatio = 0.65f;

    // Weirdness
    config.weirdness.dimensionName = "Weirdness";
    config.weirdness.baseLayer = NoiseLayerConfig(
        "Weirdness Base",
        FastNoiseLite::NoiseType_OpenSimplex2,
        0.0005f,  // ~2000 block features
        4, 2.5f, 0.6f
    );
    config.weirdness.detailLayer = NoiseLayerConfig(
        "Weirdness Detail",
        FastNoiseLite::NoiseType_Perlin,
        0.004f,   // ~250 block features
        2, 2.0f, 0.5f
    );
    config.weirdness.blendRatio = 0.6f;

    // Erosion
    config.erosion.dimensionName = "Erosion";
    config.erosion.baseLayer = NoiseLayerConfig(
        "Erosion Base",
        FastNoiseLite::NoiseType_OpenSimplex2,
        0.0008f,  // ~1250 block features
        4, 2.3f, 0.5f
    );
    config.erosion.baseLayer.fractalType = FastNoiseLite::FractalType_Ridged;
    config.erosion.detailLayer = NoiseLayerConfig(
        "Erosion Detail",
        FastNoiseLite::NoiseType_OpenSimplex2,
        0.005f,   // ~200 block features
        3, 2.0f, 0.5f
    );
    config.erosion.blendRatio = 0.55f;

    return config;
}

/**
 * LOCAL SCALE
 * - Medium biomes spanning 500-1000 blocks
 * - Frequent transitions
 * - High variety in small areas
 */
inline BiomeNoiseConfig createLocalConfig() {
    BiomeNoiseConfig config;
    config.configName = "Local Scale";
    config.description = "Medium biomes with frequent transitions and high variety";

    // Temperature
    config.temperature.dimensionName = "Temperature";
    config.temperature.baseLayer = NoiseLayerConfig(
        "Temperature Base",
        FastNoiseLite::NoiseType_OpenSimplex2,
        0.0012f,  // ~833 block features
        4, 2.2f, 0.55f
    );
    config.temperature.detailLayer = NoiseLayerConfig(
        "Temperature Detail",
        FastNoiseLite::NoiseType_OpenSimplex2,
        0.008f,   // ~125 block features
        3, 2.0f, 0.5f
    );
    config.temperature.blendRatio = 0.6f;

    // Moisture
    config.moisture.dimensionName = "Moisture";
    config.moisture.baseLayer = NoiseLayerConfig(
        "Moisture Base",
        FastNoiseLite::NoiseType_OpenSimplex2,
        0.0015f,  // ~666 block features
        4, 2.2f, 0.55f
    );
    config.moisture.detailLayer = NoiseLayerConfig(
        "Moisture Detail",
        FastNoiseLite::NoiseType_OpenSimplex2,
        0.010f,   // ~100 block features
        3, 2.0f, 0.5f
    );
    config.moisture.blendRatio = 0.6f;

    // Weirdness
    config.weirdness.dimensionName = "Weirdness";
    config.weirdness.baseLayer = NoiseLayerConfig(
        "Weirdness Base",
        FastNoiseLite::NoiseType_OpenSimplex2,
        0.0010f,  // ~1000 block features
        4, 2.5f, 0.6f
    );
    config.weirdness.detailLayer = NoiseLayerConfig(
        "Weirdness Detail",
        FastNoiseLite::NoiseType_Perlin,
        0.008f,   // ~125 block features
        2, 2.0f, 0.5f
    );
    config.weirdness.blendRatio = 0.55f;

    // Erosion
    config.erosion.dimensionName = "Erosion";
    config.erosion.baseLayer = NoiseLayerConfig(
        "Erosion Base",
        FastNoiseLite::NoiseType_OpenSimplex2,
        0.0018f,  // ~555 block features
        4, 2.3f, 0.5f
    );
    config.erosion.baseLayer.fractalType = FastNoiseLite::FractalType_Ridged;
    config.erosion.detailLayer = NoiseLayerConfig(
        "Erosion Detail",
        FastNoiseLite::NoiseType_OpenSimplex2,
        0.012f,   // ~83 block features
        3, 2.0f, 0.5f
    );
    config.erosion.blendRatio = 0.5f;

    config.weirdnessInfluence = 0.4f;  // Higher for more variety
    config.erosionInfluence = 0.2f;

    return config;
}

/**
 * COMPACT SCALE
 * - Small biomes spanning 200-400 blocks
 * - Very frequent transitions
 * - Maximum variety in minimal space
 */
inline BiomeNoiseConfig createCompactConfig() {
    BiomeNoiseConfig config;
    config.configName = "Compact Scale";
    config.description = "Small biomes with very frequent transitions and maximum variety";

    // Temperature
    config.temperature.dimensionName = "Temperature";
    config.temperature.baseLayer = NoiseLayerConfig(
        "Temperature Base",
        FastNoiseLite::NoiseType_OpenSimplex2,
        0.0025f,  // ~400 block features
        4, 2.0f, 0.5f
    );
    config.temperature.detailLayer = NoiseLayerConfig(
        "Temperature Detail",
        FastNoiseLite::NoiseType_OpenSimplex2,
        0.015f,   // ~66 block features
        3, 2.0f, 0.5f
    );
    config.temperature.blendRatio = 0.55f;

    // Moisture
    config.moisture.dimensionName = "Moisture";
    config.moisture.baseLayer = NoiseLayerConfig(
        "Moisture Base",
        FastNoiseLite::NoiseType_OpenSimplex2,
        0.003f,   // ~333 block features
        4, 2.0f, 0.5f
    );
    config.moisture.detailLayer = NoiseLayerConfig(
        "Moisture Detail",
        FastNoiseLite::NoiseType_OpenSimplex2,
        0.018f,   // ~55 block features
        3, 2.0f, 0.5f
    );
    config.moisture.blendRatio = 0.55f;

    // Weirdness
    config.weirdness.dimensionName = "Weirdness";
    config.weirdness.baseLayer = NoiseLayerConfig(
        "Weirdness Base",
        FastNoiseLite::NoiseType_OpenSimplex2,
        0.0020f,  // ~500 block features
        3, 2.5f, 0.6f
    );
    config.weirdness.detailLayer = NoiseLayerConfig(
        "Weirdness Detail",
        FastNoiseLite::NoiseType_Perlin,
        0.015f,   // ~66 block features
        2, 2.0f, 0.5f
    );
    config.weirdness.blendRatio = 0.5f;

    // Erosion
    config.erosion.dimensionName = "Erosion";
    config.erosion.baseLayer = NoiseLayerConfig(
        "Erosion Base",
        FastNoiseLite::NoiseType_OpenSimplex2,
        0.004f,   // ~250 block features
        3, 2.3f, 0.5f
    );
    config.erosion.baseLayer.fractalType = FastNoiseLite::FractalType_Ridged;
    config.erosion.detailLayer = NoiseLayerConfig(
        "Erosion Detail",
        FastNoiseLite::NoiseType_OpenSimplex2,
        0.025f,   // ~40 block features
        2, 2.0f, 0.5f
    );
    config.erosion.blendRatio = 0.45f;

    config.weirdnessInfluence = 0.5f;  // Maximum variety
    config.erosionInfluence = 0.25f;

    return config;
}

} // namespace BiomeNoise
