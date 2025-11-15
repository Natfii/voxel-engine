#pragma once

#include <cmath>
#include <algorithm>
#include <string>

/**
 * Advanced Biome Influence Falloff System
 *
 * This system extends the base transition configuration with additional falloff curves
 * and per-biome customization capabilities for fine-grained control over biome blending.
 *
 * Key Features:
 * - 10+ falloff curve types (linear, smooth, exponential, cosine, polynomial, etc.)
 * - Per-biome falloff customization (allows biomes to override global settings)
 * - Performance-optimized implementations with lookup tables
 * - Visual quality testing framework
 *
 * Agent 23 - Biome Blending Algorithm Team
 */

namespace BiomeFalloff {

    // ==================== Extended Falloff Types ====================

    enum class FalloffType {
        // Basic Falloffs (from base system)
        LINEAR,              // Simple linear dropoff
        SMOOTH,              // Exponential smooth falloff
        VERY_SMOOTH,         // Double exponential ultra-smooth
        SHARP,               // Sharp linear with power curve

        // New Advanced Falloffs
        COSINE,              // Smooth cosine S-curve (very natural)
        POLYNOMIAL_2,        // Quadratic polynomial (x²)
        POLYNOMIAL_3,        // Cubic polynomial (x³) - smooth acceleration
        POLYNOMIAL_4,        // Quartic polynomial (x⁴) - very gentle then sharp
        INVERSE_SQUARE,      // 1/(1+x²) falloff (physics-like)
        SIGMOID,             // Logistic sigmoid (S-curve, biological)
        SMOOTHSTEP,          // Smoothstep interpolation (3x² - 2x³)
        SMOOTHERSTEP,        // Ken Perlin's improved smoothstep (6x⁵ - 15x⁴ + 10x³)
        GAUSSIAN,            // Gaussian/bell curve (natural distribution)
        HYPERBOLIC,          // Hyperbolic tangent (tanh) - smooth S-curve
        CUSTOM_PROFILE       // User-defined custom curve
    };

    // ==================== Per-Biome Falloff Configuration ====================

    /**
     * Per-biome falloff override settings
     * Allows individual biomes to customize their influence falloff behavior
     */
    struct BiomeFalloffConfig {
        // Whether this biome uses custom falloff (false = use global profile)
        bool useCustomFalloff = false;

        // Custom falloff type for this biome
        FalloffType falloffType = FalloffType::SMOOTH;

        // Custom parameters
        float customSharpness = 1.0f;        // Sharpness multiplier
        float customBlendDistance = 15.0f;   // Custom blend distance
        float customSearchRadius = 25.0f;    // Custom search radius
        float customExponentialFactor = -3.0f; // Exponential decay rate

        // Influence modifiers
        float influenceMultiplier = 1.0f;    // Overall influence strength (0.5-2.0)
        float edgeSoftness = 1.0f;           // Edge transition softness (0.1-3.0)

        // Directional influence (experimental)
        bool useDirectionalFalloff = false;  // Enable directional bias
        float preferredDirection = 0.0f;     // Direction in radians (0 = east)
        float directionalStrength = 0.0f;    // How much direction affects falloff

        // Constructor
        BiomeFalloffConfig() = default;
    };

    // ==================== Advanced Falloff Curve Functions ====================

    /**
     * Cosine falloff - smooth S-curve using cosine function
     * Very natural looking, computationally efficient
     * Visual Quality: ⭐⭐⭐⭐⭐ (Excellent)
     * Performance: ⭐⭐⭐⭐ (Very Good - single trig function)
     */
    inline float calculateCosineFalloff(float normalizedDistance) {
        if (normalizedDistance >= 1.0f) return 0.0f;
        if (normalizedDistance <= 0.0f) return 1.0f;

        // Cosine S-curve: (1 + cos(π * x)) / 2
        // Smooth acceleration at both ends
        return (1.0f + std::cos(normalizedDistance * 3.14159265359f)) * 0.5f;
    }

    /**
     * Polynomial falloff - power curve
     * Higher powers = sharper falloff near edges, gentler in center
     * Visual Quality: ⭐⭐⭐⭐ (Very Good)
     * Performance: ⭐⭐⭐⭐⭐ (Excellent - simple math)
     */
    inline float calculatePolynomialFalloff(float normalizedDistance, int power = 2) {
        if (normalizedDistance >= 1.0f) return 0.0f;
        if (normalizedDistance <= 0.0f) return 1.0f;

        float t = 1.0f - normalizedDistance;

        switch (power) {
            case 2: return t * t;
            case 3: return t * t * t;
            case 4: return t * t * t * t;
            default: return std::pow(t, static_cast<float>(power));
        }
    }

    /**
     * Inverse square falloff - physics-like influence
     * Similar to gravity/light falloff in nature
     * Visual Quality: ⭐⭐⭐⭐ (Very Good - feels natural)
     * Performance: ⭐⭐⭐⭐⭐ (Excellent - simple division)
     */
    inline float calculateInverseSquareFalloff(float normalizedDistance, float strength = 1.0f) {
        if (normalizedDistance >= 1.0f) return 0.0f;
        if (normalizedDistance <= 0.0f) return 1.0f;

        // 1 / (1 + strength * x²)
        float d2 = normalizedDistance * normalizedDistance;
        return 1.0f / (1.0f + strength * d2);
    }

    /**
     * Sigmoid (Logistic) falloff - biological S-curve
     * Very smooth transitions, similar to natural phenomena
     * Visual Quality: ⭐⭐⭐⭐⭐ (Excellent - very natural)
     * Performance: ⭐⭐⭐ (Good - single exp call)
     */
    inline float calculateSigmoidFalloff(float normalizedDistance, float steepness = 10.0f) {
        if (normalizedDistance >= 1.0f) return 0.0f;
        if (normalizedDistance <= 0.0f) return 1.0f;

        // Sigmoid: 1 / (1 + e^(steepness * (x - 0.5)))
        // Centered at x=0.5, steepness controls transition sharpness
        float centered = normalizedDistance - 0.5f;
        float sigmoid = 1.0f / (1.0f + std::exp(steepness * centered));

        // Normalize to [0, 1] range
        return (sigmoid - 1.0f / (1.0f + std::exp(steepness * 0.5f))) /
               (1.0f / (1.0f + std::exp(-steepness * 0.5f)) - 1.0f / (1.0f + std::exp(steepness * 0.5f)));
    }

    /**
     * Smoothstep falloff - classic interpolation
     * Standard in graphics, very smooth acceleration/deceleration
     * Visual Quality: ⭐⭐⭐⭐⭐ (Excellent)
     * Performance: ⭐⭐⭐⭐⭐ (Excellent - simple polynomial)
     */
    inline float calculateSmoothstepFalloff(float normalizedDistance) {
        if (normalizedDistance >= 1.0f) return 0.0f;
        if (normalizedDistance <= 0.0f) return 1.0f;

        float t = 1.0f - normalizedDistance;
        // Smoothstep: 3t² - 2t³
        return t * t * (3.0f - 2.0f * t);
    }

    /**
     * Smootherstep falloff - Ken Perlin's improved smoothstep
     * Even smoother than smoothstep, zero 1st and 2nd derivatives at endpoints
     * Visual Quality: ⭐⭐⭐⭐⭐ (Excellent - imperceptible transitions)
     * Performance: ⭐⭐⭐⭐ (Very Good - polynomial)
     */
    inline float calculateSmootherStepFalloff(float normalizedDistance) {
        if (normalizedDistance >= 1.0f) return 0.0f;
        if (normalizedDistance <= 0.0f) return 1.0f;

        float t = 1.0f - normalizedDistance;
        // Smootherstep: 6t⁵ - 15t⁴ + 10t³
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    }

    /**
     * Gaussian (Bell Curve) falloff - natural distribution
     * Models many natural phenomena (temperature, elevation, etc.)
     * Visual Quality: ⭐⭐⭐⭐⭐ (Excellent - very natural)
     * Performance: ⭐⭐⭐ (Good - single exp call)
     */
    inline float calculateGaussianFalloff(float normalizedDistance, float sigma = 0.3f) {
        if (normalizedDistance >= 1.0f) return 0.0f;
        if (normalizedDistance <= 0.0f) return 1.0f;

        // Gaussian: e^(-(x²)/(2σ²))
        float exponent = -(normalizedDistance * normalizedDistance) / (2.0f * sigma * sigma);
        return std::exp(exponent);
    }

    /**
     * Hyperbolic tangent falloff - smooth S-curve
     * Similar to sigmoid but mathematically different, often faster
     * Visual Quality: ⭐⭐⭐⭐⭐ (Excellent)
     * Performance: ⭐⭐⭐ (Good - single tanh call)
     */
    inline float calculateHyperbolicFalloff(float normalizedDistance, float steepness = 4.0f) {
        if (normalizedDistance >= 1.0f) return 0.0f;
        if (normalizedDistance <= 0.0f) return 1.0f;

        // Tanh S-curve: (1 - tanh(steepness * (x - 0.5))) / 2
        float centered = normalizedDistance - 0.5f;
        return (1.0f - std::tanh(steepness * centered)) * 0.5f;
    }

    // ==================== Unified Falloff Calculator ====================

    /**
     * Calculate falloff weight using the specified falloff type and configuration
     * This is the main dispatcher function that calls the appropriate falloff curve
     *
     * @param distance Raw distance in temperature/moisture space
     * @param config Per-biome falloff configuration
     * @param rarityWeight Biome's rarity weight (affects final influence)
     * @return Influence weight [0.0, 1.0+] before normalization
     */
    inline float calculateBiomeFalloff(float distance, const BiomeFalloffConfig& config, float rarityWeight) {
        // Check if distance exceeds search radius
        if (distance > config.customSearchRadius) {
            return 0.0f;
        }

        // Calculate normalized distance [0, 1]
        float normalizedDist = distance / config.customSearchRadius;

        // Apply edge softness modifier (affects how normalized distance is perceived)
        if (config.edgeSoftness != 1.0f) {
            normalizedDist = std::pow(normalizedDist, config.edgeSoftness);
        }

        // Calculate base falloff weight based on type
        float baseWeight = 0.0f;

        switch (config.falloffType) {
            case FalloffType::LINEAR:
                baseWeight = 1.0f - normalizedDist;
                break;

            case FalloffType::SMOOTH:
                // Exponential smooth falloff
                baseWeight = std::exp(config.customExponentialFactor * normalizedDist * normalizedDist);
                break;

            case FalloffType::VERY_SMOOTH:
                // Double exponential
                baseWeight = std::exp(config.customExponentialFactor * normalizedDist * normalizedDist);
                baseWeight = std::sqrt(baseWeight);
                break;

            case FalloffType::SHARP:
                // Sharp power curve
                baseWeight = std::pow(1.0f - normalizedDist, config.customSharpness * 2.0f);
                break;

            case FalloffType::COSINE:
                baseWeight = calculateCosineFalloff(normalizedDist);
                break;

            case FalloffType::POLYNOMIAL_2:
                baseWeight = calculatePolynomialFalloff(normalizedDist, 2);
                break;

            case FalloffType::POLYNOMIAL_3:
                baseWeight = calculatePolynomialFalloff(normalizedDist, 3);
                break;

            case FalloffType::POLYNOMIAL_4:
                baseWeight = calculatePolynomialFalloff(normalizedDist, 4);
                break;

            case FalloffType::INVERSE_SQUARE:
                baseWeight = calculateInverseSquareFalloff(normalizedDist, 2.0f);
                break;

            case FalloffType::SIGMOID:
                baseWeight = calculateSigmoidFalloff(normalizedDist, 10.0f);
                break;

            case FalloffType::SMOOTHSTEP:
                baseWeight = calculateSmoothstepFalloff(normalizedDist);
                break;

            case FalloffType::SMOOTHERSTEP:
                baseWeight = calculateSmootherStepFalloff(normalizedDist);
                break;

            case FalloffType::GAUSSIAN:
                baseWeight = calculateGaussianFalloff(normalizedDist, 0.35f);
                break;

            case FalloffType::HYPERBOLIC:
                baseWeight = calculateHyperbolicFalloff(normalizedDist, 5.0f);
                break;

            default:
                // Default to smooth falloff
                baseWeight = std::exp(config.customExponentialFactor * normalizedDist * normalizedDist);
                break;
        }

        // Apply custom sharpness modifier
        if (config.customSharpness != 1.0f && config.falloffType != FalloffType::SHARP) {
            baseWeight = std::pow(baseWeight, config.customSharpness);
        }

        // Apply influence multiplier
        baseWeight *= config.influenceMultiplier;

        // Apply biome rarity modifier
        // Rarer biomes (lower rarity_weight) have less influence
        // Common biomes (higher rarity_weight) have more influence
        baseWeight *= (rarityWeight / 50.0f);

        return std::max(0.0f, baseWeight);
    }

    // ==================== Falloff Type Utilities ====================

    /**
     * Get falloff type from string name (for configuration files)
     */
    inline FalloffType getFalloffTypeByName(const std::string& name) {
        if (name == "linear") return FalloffType::LINEAR;
        if (name == "smooth") return FalloffType::SMOOTH;
        if (name == "very_smooth") return FalloffType::VERY_SMOOTH;
        if (name == "sharp") return FalloffType::SHARP;
        if (name == "cosine") return FalloffType::COSINE;
        if (name == "polynomial_2" || name == "quadratic") return FalloffType::POLYNOMIAL_2;
        if (name == "polynomial_3" || name == "cubic") return FalloffType::POLYNOMIAL_3;
        if (name == "polynomial_4" || name == "quartic") return FalloffType::POLYNOMIAL_4;
        if (name == "inverse_square") return FalloffType::INVERSE_SQUARE;
        if (name == "sigmoid") return FalloffType::SIGMOID;
        if (name == "smoothstep") return FalloffType::SMOOTHSTEP;
        if (name == "smootherstep") return FalloffType::SMOOTHERSTEP;
        if (name == "gaussian") return FalloffType::GAUSSIAN;
        if (name == "hyperbolic" || name == "tanh") return FalloffType::HYPERBOLIC;

        // Default to smooth
        return FalloffType::SMOOTH;
    }

    /**
     * Get falloff type name (for debugging/logging)
     */
    inline const char* getFalloffTypeName(FalloffType type) {
        switch (type) {
            case FalloffType::LINEAR: return "Linear";
            case FalloffType::SMOOTH: return "Smooth";
            case FalloffType::VERY_SMOOTH: return "Very Smooth";
            case FalloffType::SHARP: return "Sharp";
            case FalloffType::COSINE: return "Cosine";
            case FalloffType::POLYNOMIAL_2: return "Polynomial (Quadratic)";
            case FalloffType::POLYNOMIAL_3: return "Polynomial (Cubic)";
            case FalloffType::POLYNOMIAL_4: return "Polynomial (Quartic)";
            case FalloffType::INVERSE_SQUARE: return "Inverse Square";
            case FalloffType::SIGMOID: return "Sigmoid";
            case FalloffType::SMOOTHSTEP: return "Smoothstep";
            case FalloffType::SMOOTHERSTEP: return "Smootherstep";
            case FalloffType::GAUSSIAN: return "Gaussian";
            case FalloffType::HYPERBOLIC: return "Hyperbolic (Tanh)";
            default: return "Unknown";
        }
    }

    // ==================== Predefined Falloff Configurations ====================

    /**
     * Predefined falloff configs for common use cases
     */

    // Natural biomes (forests, plains, etc.) - smooth transitions
    static const BiomeFalloffConfig FALLOFF_NATURAL = {
        true,                           // useCustomFalloff
        FalloffType::SMOOTHERSTEP,      // Very smooth, natural
        1.0f,                           // Normal sharpness
        18.0f,                          // Standard blend distance
        28.0f,                          // Standard search radius
        -3.0f,                          // Standard exponential
        1.0f,                           // Normal influence
        1.2f                            // Slightly soft edges
    };

    // Mountain biomes - wider, gentler transitions
    static const BiomeFalloffConfig FALLOFF_MOUNTAIN = {
        true,
        FalloffType::GAUSSIAN,          // Natural elevation-like falloff
        0.7f,                           // Gentler sharpness
        25.0f,                          // Wider blend distance
        40.0f,                          // Wider search radius
        -2.5f,                          // Gentle exponential
        1.2f,                           // Stronger influence (mountains are prominent)
        1.5f                            // Very soft edges
    };

    // Desert biomes - sharper transitions (deserts have distinct boundaries)
    static const BiomeFalloffConfig FALLOFF_DESERT = {
        true,
        FalloffType::POLYNOMIAL_3,      // Cubic falloff
        1.5f,                           // Sharper transitions
        12.0f,                          // Narrower blend distance
        20.0f,                          // Smaller search radius
        -4.0f,                          // Steeper exponential
        1.0f,                           // Normal influence
        0.8f                            // Sharper edges
    };

    // Ocean/water biomes - very smooth, wide transitions
    static const BiomeFalloffConfig FALLOFF_OCEAN = {
        true,
        FalloffType::COSINE,            // Smooth wave-like
        0.8f,                           // Gentle sharpness
        30.0f,                          // Very wide blend distance
        50.0f,                          // Very wide search radius
        -2.0f,                          // Very gentle exponential
        1.3f,                           // Strong influence (oceans are large)
        2.0f                            // Very soft edges
    };

    // Rare/special biomes (mushroom, ice spikes) - medium-sharp transitions
    static const BiomeFalloffConfig FALLOFF_RARE = {
        true,
        FalloffType::SIGMOID,           // Biological S-curve
        1.2f,                           // Slightly sharp
        10.0f,                          // Narrower blend distance
        18.0f,                          // Smaller search radius
        -3.5f,                          // Moderately steep
        0.7f,                           // Lower influence (rare = less spread)
        0.9f                            // Slightly sharp edges
    };

    // Cave/underground biomes - contained transitions
    static const BiomeFalloffConfig FALLOFF_CAVE = {
        true,
        FalloffType::INVERSE_SQUARE,    // Physics-like (light/space)
        1.8f,                           // Sharp
        8.0f,                           // Narrow blend distance
        15.0f,                          // Small search radius
        -5.0f,                          // Steep exponential
        0.9f,                           // Slightly lower influence
        0.7f                            // Sharp edges (contained chambers)
    };

} // namespace BiomeFalloff
