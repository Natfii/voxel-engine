#pragma once

#include <cmath>
#include <algorithm>
#include <string>

/**
 * Biome Transition Configuration System
 *
 * This system provides tunable parameters for controlling how biomes blend together.
 * Different transition profiles offer varying levels of visual quality and performance.
 *
 * Key Concepts:
 * - Search Radius: Maximum distance (in temp/moisture space) to search for blending biomes
 * - Blend Distance: Distance where smooth falloff begins
 * - Blending Curve: Mathematical function determining how influence decreases with distance
 * - Transition Sharpness: Controls how quickly transitions occur (higher = sharper edges)
 */

namespace BiomeTransition {

    // ==================== Transition Types ====================

    enum class TransitionType {
        // Sharp transitions - minimal blending, clear biome boundaries
        // Performance: Excellent (fewer calculations)
        // Visual Quality: Basic (visible biome edges)
        SHARP,

        // Linear transitions - simple linear falloff
        // Performance: Very Good (simple math)
        // Visual Quality: Good (smooth but basic)
        LINEAR,

        // Smooth transitions - exponential falloff (default)
        // Performance: Good (single exp calculation)
        // Visual Quality: Very Good (natural looking)
        SMOOTH,

        // Very smooth transitions - double exponential
        // Performance: Fair (multiple exp calculations)
        // Visual Quality: Excellent (extremely natural)
        VERY_SMOOTH,

        // Custom transitions - user-defined curves
        // Performance: Variable
        // Visual Quality: Variable
        CUSTOM
    };

    // ==================== Transition Profiles ====================

    /**
     * Predefined transition profiles for different use cases
     */
    struct TransitionProfile {
        const char* name;
        TransitionType type;
        float searchRadius;      // Units in temperature/moisture space
        float blendDistance;     // Where smooth falloff begins
        float minInfluence;      // Minimum weight to consider
        size_t maxBiomes;        // Maximum biomes per blend point
        float sharpness;         // Transition sharpness factor (1.0 = normal, higher = sharper)
        float exponentialFactor; // For exponential curves (default: -3.0)
    };

    // ==================== Predefined Profiles ====================

    // Performance-focused: Sharp transitions, minimal blending
    static constexpr TransitionProfile PROFILE_PERFORMANCE = {
        "Performance",
        TransitionType::SHARP,
        15.0f,    // Smaller search radius
        8.0f,     // Narrow blend zone
        0.05f,    // Higher minimum influence
        2,        // Only 2 biomes max
        2.0f,     // Sharp transitions
        -4.0f     // Steep exponential
    };

    // Balanced: Good visual quality with acceptable performance (RECOMMENDED)
    static constexpr TransitionProfile PROFILE_BALANCED = {
        "Balanced",
        TransitionType::SMOOTH,
        25.0f,    // Moderate search radius
        15.0f,    // Standard blend zone
        0.01f,    // Low minimum influence
        4,        // Up to 4 biomes
        1.0f,     // Normal sharpness
        -3.0f     // Standard exponential
    };

    // Quality-focused: Maximum visual quality, more expensive
    static constexpr TransitionProfile PROFILE_QUALITY = {
        "Quality",
        TransitionType::VERY_SMOOTH,
        35.0f,    // Larger search radius
        20.0f,    // Wide blend zone
        0.005f,   // Very low minimum influence
        6,        // Up to 6 biomes
        0.7f,     // Gentler transitions
        -2.5f     // Gradual exponential
    };

    // Ultra-wide: Very wide transitions for continental-scale biomes
    static constexpr TransitionProfile PROFILE_WIDE = {
        "Wide Transitions",
        TransitionType::SMOOTH,
        50.0f,    // Very large search radius
        30.0f,    // Very wide blend zone
        0.01f,    // Standard minimum influence
        5,        // Up to 5 biomes
        0.5f,     // Very gentle transitions
        -2.0f     // Gradual exponential
    };

    // Narrow: Sharp, distinct biome boundaries
    static constexpr TransitionProfile PROFILE_NARROW = {
        "Narrow Transitions",
        TransitionType::LINEAR,
        12.0f,    // Small search radius
        5.0f,     // Narrow blend zone
        0.02f,    // Higher minimum influence
        3,        // Up to 3 biomes
        1.5f,     // Sharper transitions
        -5.0f     // Very steep exponential
    };

    // ==================== Blending Curve Functions ====================

    /**
     * Calculate influence weight using sharp transition
     * Creates clear biome boundaries with minimal blending
     */
    inline float calculateSharpWeight(float distance, float blendDistance, float searchRadius, float sharpness) {
        if (distance > searchRadius) return 0.0f;

        if (distance <= blendDistance) {
            // Inside core: full weight
            return 1.0f;
        } else {
            // Outside core: sharp linear dropoff
            float normalizedDist = (distance - blendDistance) / (searchRadius - blendDistance);
            float weight = 1.0f - normalizedDist;

            // Apply sharpness factor (power function)
            weight = std::pow(weight, sharpness);

            return std::max(0.0f, weight);
        }
    }

    /**
     * Calculate influence weight using linear transition
     * Simple linear falloff from center to edge
     */
    inline float calculateLinearWeight(float distance, float blendDistance, float searchRadius, float sharpness) {
        if (distance > searchRadius) return 0.0f;

        // Linear falloff from center to search radius
        float normalizedDist = distance / searchRadius;
        float weight = 1.0f - normalizedDist;

        // Apply sharpness factor
        if (sharpness != 1.0f) {
            weight = std::pow(weight, sharpness);
        }

        return std::max(0.0f, weight);
    }

    /**
     * Calculate influence weight using smooth exponential transition
     * Creates natural-looking biome blending with smooth S-curve
     */
    inline float calculateSmoothWeight(float distance, float blendDistance, float searchRadius,
                                        float sharpness, float exponentialFactor) {
        if (distance > searchRadius) return 0.0f;

        float weight;

        if (distance <= blendDistance) {
            // Inner zone: linear falloff
            weight = 1.0f - (distance / blendDistance);
        } else {
            // Outer zone: smooth exponential decay
            float falloffDist = distance - blendDistance;
            float falloffRange = searchRadius - blendDistance;
            float normalizedFalloff = falloffDist / falloffRange;

            // Exponential decay: e^(factor * x²)
            // Factor controls steepness: -3.0 = standard, -5.0 = sharp, -2.0 = gentle
            weight = std::exp(exponentialFactor * normalizedFalloff * normalizedFalloff);
        }

        // Apply sharpness modifier
        if (sharpness != 1.0f) {
            weight = std::pow(weight, sharpness);
        }

        return std::max(0.0f, weight);
    }

    /**
     * Calculate influence weight using very smooth double exponential
     * Creates extremely natural transitions with gradual blending
     */
    inline float calculateVerySmoothWeight(float distance, float blendDistance, float searchRadius,
                                            float sharpness, float exponentialFactor) {
        if (distance > searchRadius) return 0.0f;

        // Normalized distance [0, 1]
        float normalizedDist = distance / searchRadius;

        // Double exponential for ultra-smooth S-curve
        // First pass: smooth falloff
        float weight = std::exp(exponentialFactor * normalizedDist * normalizedDist);

        // Second pass: soften further
        weight = std::sqrt(weight);

        // Apply sharpness modifier
        if (sharpness != 1.0f) {
            weight = std::pow(weight, sharpness);
        }

        return std::max(0.0f, weight);
    }

    /**
     * Calculate influence weight using the specified transition profile
     * Central dispatcher function that calls the appropriate blending curve
     */
    inline float calculateTransitionWeight(float distance, const TransitionProfile& profile, float rarityWeight) {
        float baseWeight = 0.0f;

        switch (profile.type) {
            case TransitionType::SHARP:
                baseWeight = calculateSharpWeight(distance, profile.blendDistance,
                                                 profile.searchRadius, profile.sharpness);
                break;

            case TransitionType::LINEAR:
                baseWeight = calculateLinearWeight(distance, profile.blendDistance,
                                                  profile.searchRadius, profile.sharpness);
                break;

            case TransitionType::SMOOTH:
                baseWeight = calculateSmoothWeight(distance, profile.blendDistance,
                                                  profile.searchRadius, profile.sharpness,
                                                  profile.exponentialFactor);
                break;

            case TransitionType::VERY_SMOOTH:
                baseWeight = calculateVerySmoothWeight(distance, profile.blendDistance,
                                                       profile.searchRadius, profile.sharpness,
                                                       profile.exponentialFactor);
                break;

            case TransitionType::CUSTOM:
                // Default to smooth for custom (can be overridden)
                baseWeight = calculateSmoothWeight(distance, profile.blendDistance,
                                                  profile.searchRadius, profile.sharpness,
                                                  profile.exponentialFactor);
                break;
        }

        // Apply biome rarity modifier
        // Rarer biomes (lower rarity_weight) have less influence
        // Common biomes (higher rarity_weight) have more influence
        baseWeight *= (rarityWeight / 50.0f);

        return baseWeight;
    }

    /**
     * Get profile by name (for runtime configuration)
     */
    inline const TransitionProfile& getProfileByName(const std::string& name) {
        if (name == "Performance" || name == "performance") {
            return PROFILE_PERFORMANCE;
        } else if (name == "Balanced" || name == "balanced") {
            return PROFILE_BALANCED;
        } else if (name == "Quality" || name == "quality") {
            return PROFILE_QUALITY;
        } else if (name == "Wide" || name == "wide") {
            return PROFILE_WIDE;
        } else if (name == "Narrow" || name == "narrow") {
            return PROFILE_NARROW;
        } else {
            // Default to balanced
            return PROFILE_BALANCED;
        }
    }

} // namespace BiomeTransition
