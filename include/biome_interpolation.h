#pragma once

#include <cmath>
#include <algorithm>
#include <vector>
#include <glm/glm.hpp>

/**
 * Biome Interpolation Utilities
 *
 * Comprehensive collection of interpolation and blending functions for smooth biome transitions.
 * All functions are header-only (inline) for optimal performance in hot paths like terrain generation.
 *
 * Categories:
 * - Basic Interpolation (lerp, smoothstep, etc.)
 * - Advanced Easing Functions (cubic, quartic, exponential, etc.)
 * - Multi-Value Weighted Interpolation
 * - Color/Gradient Blending
 * - Noise-Based Variations
 * - Utility Functions
 */

namespace BiomeInterpolation {

    // ==================== Basic Interpolation ====================

    /**
     * Linear interpolation between two values
     *
     * @param a Start value
     * @param b End value
     * @param t Interpolation factor [0, 1]
     * @return Interpolated value: a + t * (b - a)
     *
     * Use case: Simple linear transitions between biome parameters
     * Example: lerp(desert_height, forest_height, 0.5) = halfway between
     */
    inline float lerp(float a, float b, float t) {
        return a + t * (b - a);
    }

    /**
     * Clamped linear interpolation
     * Ensures t is in range [0, 1] before interpolation
     *
     * @param a Start value
     * @param b End value
     * @param t Interpolation factor (will be clamped to [0, 1])
     * @return Interpolated value
     *
     * Use case: Safe interpolation when t might exceed bounds
     */
    inline float lerpClamped(float a, float b, float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        return a + t * (b - a);
    }

    /**
     * Inverse lerp - find t for a value between a and b
     *
     * @param a Start value
     * @param b End value
     * @param value Value to find t for
     * @return t such that lerp(a, b, t) = value
     *
     * Use case: Finding how far a value is between two bounds
     */
    inline float inverseLerp(float a, float b, float value) {
        if (std::abs(b - a) < 1e-6f) return 0.0f;
        return (value - a) / (b - a);
    }

    /**
     * Smoothstep interpolation (3rd order Hermite)
     * Smooth S-curve with zero derivatives at endpoints
     *
     * @param edge0 Lower edge
     * @param edge1 Upper edge
     * @param x Input value
     * @return Smoothly interpolated value [0, 1]
     *
     * Formula: 3t² - 2t³
     *
     * Use case: Smooth transitions without visible acceleration/deceleration
     * Example: Smooth biome boundary blending
     */
    inline float smoothstep(float edge0, float edge1, float x) {
        float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    /**
     * Smootherstep interpolation (5th order)
     * Even smoother than smoothstep, zero 2nd derivatives at endpoints
     *
     * @param edge0 Lower edge
     * @param edge1 Upper edge
     * @param x Input value
     * @return Very smoothly interpolated value [0, 1]
     *
     * Formula: 6t⁵ - 15t⁴ + 10t³
     *
     * Use case: Ultra-smooth biome transitions where smoothstep isn't smooth enough
     * Example: High-quality biome blending for screenshots
     */
    inline float smootherstep(float edge0, float edge1, float x) {
        float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    }

    /**
     * Cosine interpolation
     * Smooth S-curve using cosine function
     *
     * @param a Start value
     * @param b End value
     * @param t Interpolation factor [0, 1]
     * @return Interpolated value with cosine easing
     *
     * Use case: Natural-feeling transitions, similar to smoothstep but using trig
     */
    inline float cosineInterp(float a, float b, float t) {
        float mu2 = (1.0f - std::cos(t * 3.14159265f)) / 2.0f;
        return a * (1.0f - mu2) + b * mu2;
    }

    // ==================== Advanced Easing Functions ====================

    /**
     * Cubic ease-in (slow start)
     *
     * @param t Input [0, 1]
     * @return Eased value [0, 1]
     *
     * Formula: t³
     *
     * Use case: Gradual transition start, then accelerating
     */
    inline float easeInCubic(float t) {
        return t * t * t;
    }

    /**
     * Cubic ease-out (slow end)
     *
     * @param t Input [0, 1]
     * @return Eased value [0, 1]
     *
     * Formula: 1 - (1-t)³
     *
     * Use case: Fast start, then decelerating to smooth stop
     */
    inline float easeOutCubic(float t) {
        float f = 1.0f - t;
        return 1.0f - f * f * f;
    }

    /**
     * Cubic ease-in-out (slow start and end)
     *
     * @param t Input [0, 1]
     * @return Eased value [0, 1]
     *
     * Use case: Smooth acceleration and deceleration
     */
    inline float easeInOutCubic(float t) {
        if (t < 0.5f) {
            return 4.0f * t * t * t;
        } else {
            float f = 2.0f * t - 2.0f;
            return 0.5f * f * f * f + 1.0f;
        }
    }

    /**
     * Exponential ease-in
     * Very slow start, very fast end
     *
     * @param t Input [0, 1]
     * @return Eased value [0, 1]
     *
     * Use case: Dramatic acceleration in transitions
     */
    inline float easeInExpo(float t) {
        return t == 0.0f ? 0.0f : std::pow(2.0f, 10.0f * (t - 1.0f));
    }

    /**
     * Exponential ease-out
     * Very fast start, very slow end
     *
     * @param t Input [0, 1]
     * @return Eased value [0, 1]
     *
     * Use case: Rapid initial change, then gradual settling
     */
    inline float easeOutExpo(float t) {
        return t == 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
    }

    /**
     * Circular ease-in (quarter circle)
     *
     * @param t Input [0, 1]
     * @return Eased value [0, 1]
     *
     * Use case: Natural arc-like transitions
     */
    inline float easeInCirc(float t) {
        return 1.0f - std::sqrt(1.0f - t * t);
    }

    /**
     * Circular ease-out
     *
     * @param t Input [0, 1]
     * @return Eased value [0, 1]
     */
    inline float easeOutCirc(float t) {
        float f = t - 1.0f;
        return std::sqrt(1.0f - f * f);
    }

    // ==================== Multi-Value Weighted Interpolation ====================

    /**
     * Weighted average of multiple values
     * Weights must sum to 1.0 (or will be normalized)
     *
     * @param values Array of values to blend
     * @param weights Array of weights (same size as values)
     * @param count Number of values/weights
     * @param normalizeWeights If true, weights will be normalized to sum to 1.0
     * @return Weighted average
     *
     * Use case: Blending terrain height from multiple biomes
     * Example: weightedAverage([100, 80, 60], [0.5, 0.3, 0.2]) = 87
     */
    inline float weightedAverage(const float* values, const float* weights, size_t count, bool normalizeWeights = false) {
        if (count == 0) return 0.0f;
        if (count == 1) return values[0];

        float sum = 0.0f;
        float weightSum = 0.0f;

        for (size_t i = 0; i < count; ++i) {
            sum += values[i] * weights[i];
            weightSum += weights[i];
        }

        if (normalizeWeights && weightSum > 0.0f) {
            return sum / weightSum;
        }

        return sum;
    }

    /**
     * Weighted average using std::vector (convenience overload)
     *
     * @param values Vector of values
     * @param weights Vector of weights
     * @param normalizeWeights If true, weights will be normalized
     * @return Weighted average
     */
    inline float weightedAverage(const std::vector<float>& values, const std::vector<float>& weights, bool normalizeWeights = false) {
        if (values.empty() || values.size() != weights.size()) return 0.0f;
        return weightedAverage(values.data(), weights.data(), values.size(), normalizeWeights);
    }

    /**
     * Weighted average of integers (returns float)
     * Useful for blending discrete biome properties
     *
     * @param values Array of integer values
     * @param weights Array of weights
     * @param count Number of values/weights
     * @param normalizeWeights If true, weights will be normalized
     * @return Weighted average as float
     *
     * Use case: Blending tree density (0-100) from multiple biomes
     */
    inline float weightedAverageInt(const int* values, const float* weights, size_t count, bool normalizeWeights = false) {
        if (count == 0) return 0.0f;
        if (count == 1) return static_cast<float>(values[0]);

        float sum = 0.0f;
        float weightSum = 0.0f;

        for (size_t i = 0; i < count; ++i) {
            sum += static_cast<float>(values[i]) * weights[i];
            weightSum += weights[i];
        }

        if (normalizeWeights && weightSum > 0.0f) {
            return sum / weightSum;
        }

        return sum;
    }

    /**
     * Normalize weights to sum to 1.0
     * Modifies weights in-place
     *
     * @param weights Array of weights to normalize
     * @param count Number of weights
     *
     * Use case: Ensuring biome influences sum to 100%
     */
    inline void normalizeWeights(float* weights, size_t count) {
        if (count == 0) return;

        float sum = 0.0f;
        for (size_t i = 0; i < count; ++i) {
            sum += weights[i];
        }

        if (sum > 0.0f) {
            float invSum = 1.0f / sum;
            for (size_t i = 0; i < count; ++i) {
                weights[i] *= invSum;
            }
        }
    }

    // ==================== Color/Gradient Blending ====================

    /**
     * Linear RGB color interpolation
     *
     * @param color1 Start color (RGB)
     * @param color2 End color (RGB)
     * @param t Interpolation factor [0, 1]
     * @return Blended color
     *
     * Use case: Blending fog colors between biomes
     */
    inline glm::vec3 lerpColor(const glm::vec3& color1, const glm::vec3& color2, float t) {
        return glm::vec3(
            lerp(color1.r, color2.r, t),
            lerp(color1.g, color2.g, t),
            lerp(color1.b, color2.b, t)
        );
    }

    /**
     * Smooth RGB color interpolation using smoothstep
     *
     * @param color1 Start color
     * @param color2 End color
     * @param t Interpolation factor [0, 1]
     * @return Smoothly blended color
     *
     * Use case: Smooth fog color transitions at biome boundaries
     */
    inline glm::vec3 smoothColorBlend(const glm::vec3& color1, const glm::vec3& color2, float t) {
        t = smoothstep(0.0f, 1.0f, t);
        return lerpColor(color1, color2, t);
    }

    /**
     * Weighted average of multiple colors
     *
     * @param colors Array of colors
     * @param weights Array of weights (must sum to 1.0 or use normalize)
     * @param count Number of colors/weights
     * @param normalizeWeights If true, weights will be normalized
     * @return Blended color
     *
     * Use case: Blending fog colors from 3-4 neighboring biomes
     */
    inline glm::vec3 weightedColorAverage(const glm::vec3* colors, const float* weights, size_t count, bool normalizeWeights = false) {
        if (count == 0) return glm::vec3(0.0f);
        if (count == 1) return colors[0];

        glm::vec3 result(0.0f);
        float weightSum = 0.0f;

        for (size_t i = 0; i < count; ++i) {
            result += colors[i] * weights[i];
            weightSum += weights[i];
        }

        if (normalizeWeights && weightSum > 0.0f) {
            result /= weightSum;
        }

        return result;
    }

    /**
     * HSV to RGB conversion
     *
     * @param h Hue [0, 360]
     * @param s Saturation [0, 1]
     * @param v Value [0, 1]
     * @return RGB color
     *
     * Use case: Creating gradient colors for biome transitions
     */
    inline glm::vec3 hsvToRgb(float h, float s, float v) {
        if (s <= 0.0f) return glm::vec3(v);

        h = std::fmod(h, 360.0f);
        if (h < 0.0f) h += 360.0f;

        float c = v * s;
        float x = c * (1.0f - std::abs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
        float m = v - c;

        glm::vec3 rgb;
        if (h < 60.0f) {
            rgb = glm::vec3(c, x, 0.0f);
        } else if (h < 120.0f) {
            rgb = glm::vec3(x, c, 0.0f);
        } else if (h < 180.0f) {
            rgb = glm::vec3(0.0f, c, x);
        } else if (h < 240.0f) {
            rgb = glm::vec3(0.0f, x, c);
        } else if (h < 300.0f) {
            rgb = glm::vec3(x, 0.0f, c);
        } else {
            rgb = glm::vec3(c, 0.0f, x);
        }

        return rgb + glm::vec3(m);
    }

    /**
     * RGB to HSV conversion
     *
     * @param rgb RGB color [0, 1] for each component
     * @return HSV color (h: [0, 360], s: [0, 1], v: [0, 1])
     *
     * Use case: Color space conversion for advanced blending
     */
    inline glm::vec3 rgbToHsv(const glm::vec3& rgb) {
        float maxC = std::max({rgb.r, rgb.g, rgb.b});
        float minC = std::min({rgb.r, rgb.g, rgb.b});
        float delta = maxC - minC;

        glm::vec3 hsv;
        hsv.z = maxC; // Value

        if (delta < 0.00001f) {
            hsv.x = 0.0f; // Hue
            hsv.y = 0.0f; // Saturation
            return hsv;
        }

        if (maxC > 0.0f) {
            hsv.y = delta / maxC; // Saturation
        } else {
            hsv.y = 0.0f;
            hsv.x = 0.0f;
            return hsv;
        }

        // Hue calculation
        if (rgb.r >= maxC) {
            hsv.x = (rgb.g - rgb.b) / delta;
        } else if (rgb.g >= maxC) {
            hsv.x = 2.0f + (rgb.b - rgb.r) / delta;
        } else {
            hsv.x = 4.0f + (rgb.r - rgb.g) / delta;
        }

        hsv.x *= 60.0f;
        if (hsv.x < 0.0f) hsv.x += 360.0f;

        return hsv;
    }

    /**
     * HSV-based color interpolation
     * Interpolates through hue space for more natural color transitions
     *
     * @param color1 Start color (RGB)
     * @param color2 End color (RGB)
     * @param t Interpolation factor [0, 1]
     * @return Blended color (RGB)
     *
     * Use case: Natural color transitions (e.g., sunset gradients)
     */
    inline glm::vec3 lerpColorHSV(const glm::vec3& color1, const glm::vec3& color2, float t) {
        glm::vec3 hsv1 = rgbToHsv(color1);
        glm::vec3 hsv2 = rgbToHsv(color2);

        // Handle hue wraparound (shortest path around color wheel)
        float hueDiff = hsv2.x - hsv1.x;
        if (hueDiff > 180.0f) {
            hsv2.x -= 360.0f;
        } else if (hueDiff < -180.0f) {
            hsv2.x += 360.0f;
        }

        glm::vec3 hsvBlend(
            lerp(hsv1.x, hsv2.x, t),
            lerp(hsv1.y, hsv2.y, t),
            lerp(hsv1.z, hsv2.z, t)
        );

        return hsvToRgb(hsvBlend.x, hsvBlend.y, hsvBlend.z);
    }

    // ==================== Noise-Based Variations ====================

    /**
     * Apply random variation to a value using noise
     *
     * @param baseValue Base value to vary
     * @param noiseValue Noise input [-1, 1] or [0, 1]
     * @param variationAmount How much variation to apply (0 = none, 1 = full range)
     * @return Varied value
     *
     * Use case: Adding natural variation to blended biome properties
     * Example: Varying tree density slightly even in uniform biome
     */
    inline float applyNoiseVariation(float baseValue, float noiseValue, float variationAmount) {
        // Map noise from [-1, 1] to [-variationAmount, +variationAmount]
        float variation = noiseValue * variationAmount * baseValue;
        return baseValue + variation;
    }

    /**
     * Apply asymmetric noise variation (positive or negative bias)
     *
     * @param baseValue Base value
     * @param noiseValue Noise input [0, 1]
     * @param maxIncrease Maximum increase factor (e.g., 0.2 = up to 20% increase)
     * @param maxDecrease Maximum decrease factor (e.g., 0.3 = up to 30% decrease)
     * @return Varied value
     *
     * Use case: Non-uniform variation (e.g., trees can decrease more than increase)
     */
    inline float applyAsymmetricVariation(float baseValue, float noiseValue, float maxIncrease, float maxDecrease) {
        if (noiseValue > 0.5f) {
            // Increase
            float t = (noiseValue - 0.5f) * 2.0f;
            return baseValue * (1.0f + t * maxIncrease);
        } else {
            // Decrease
            float t = (0.5f - noiseValue) * 2.0f;
            return baseValue * (1.0f - t * maxDecrease);
        }
    }

    /**
     * Create local variation hotspots using noise
     * High noise values create "pockets" of different properties
     *
     * @param baseValue Base value
     * @param noiseValue Noise input [0, 1]
     * @param threshold Threshold above which variation applies [0, 1]
     * @param variationValue Value to blend to in hotspots
     * @return Varied value
     *
     * Use case: Creating patches of different features within a biome
     * Example: Dense tree clusters in otherwise sparse forest
     */
    inline float createVariationHotspot(float baseValue, float noiseValue, float threshold, float variationValue) {
        if (noiseValue < threshold) {
            return baseValue;
        }

        // Above threshold: blend toward variation value
        float t = (noiseValue - threshold) / (1.0f - threshold);
        t = smoothstep(0.0f, 1.0f, t); // Smooth transition
        return lerp(baseValue, variationValue, t);
    }

    /**
     * Turbulence function - layered absolute noise
     * Creates irregular, turbulent patterns
     *
     * @param noiseValues Array of noise values from different octaves
     * @param octaveCount Number of octaves
     * @param persistence How much each octave contributes (typically 0.5)
     * @return Turbulent value
     *
     * Use case: Creating irregular biome boundaries, chaotic terrain features
     */
    inline float turbulence(const float* noiseValues, size_t octaveCount, float persistence) {
        float total = 0.0f;
        float amplitude = 1.0f;
        float maxValue = 0.0f;

        for (size_t i = 0; i < octaveCount; ++i) {
            total += std::abs(noiseValues[i]) * amplitude;
            maxValue += amplitude;
            amplitude *= persistence;
        }

        return total / maxValue; // Normalize
    }

    /**
     * Ridged multifractal noise
     * Creates ridge-like patterns, useful for erosion effects
     *
     * @param noiseValue Base noise value [-1, 1]
     * @param sharpness Controls ridge sharpness (typically 1.0-3.0)
     * @return Ridged value [0, 1]
     *
     * Use case: Erosion patterns, mountain ridges, river valleys
     */
    inline float ridgedNoise(float noiseValue, float sharpness) {
        float n = 1.0f - std::abs(noiseValue);
        return std::pow(n, sharpness);
    }

    // ==================== Utility Functions ====================

    /**
     * Remap value from one range to another
     *
     * @param value Input value
     * @param fromMin Input range minimum
     * @param fromMax Input range maximum
     * @param toMin Output range minimum
     * @param toMax Output range maximum
     * @return Remapped value
     *
     * Use case: Converting noise values to desired range
     * Example: remap(noiseValue, -1, 1, 0, 100) converts [-1,1] to [0,100]
     */
    inline float remap(float value, float fromMin, float fromMax, float toMin, float toMax) {
        float t = inverseLerp(fromMin, fromMax, value);
        return lerp(toMin, toMax, t);
    }

    /**
     * Clamped remap - ensures output stays within target range
     *
     * @param value Input value
     * @param fromMin Input range minimum
     * @param fromMax Input range maximum
     * @param toMin Output range minimum
     * @param toMax Output range maximum
     * @return Remapped and clamped value
     */
    inline float remapClamped(float value, float fromMin, float fromMax, float toMin, float toMax) {
        float t = inverseLerp(fromMin, fromMax, value);
        t = std::clamp(t, 0.0f, 1.0f);
        return lerp(toMin, toMax, t);
    }

    /**
     * Bias function - shifts midpoint of interpolation
     *
     * @param t Input [0, 1]
     * @param bias Bias factor (0.5 = no bias, <0.5 = shift down, >0.5 = shift up)
     * @return Biased value [0, 1]
     *
     * Use case: Adjusting where transitions occur
     */
    inline float bias(float t, float bias) {
        return t / ((1.0f / bias - 2.0f) * (1.0f - t) + 1.0f);
    }

    /**
     * Gain function - adjusts curve shape (S-curve intensity)
     *
     * @param t Input [0, 1]
     * @param gain Gain factor (0.5 = linear, <0.5 = ease out, >0.5 = ease in)
     * @return Gained value [0, 1]
     *
     * Use case: Fine-tuning transition curves
     */
    inline float gain(float t, float gain) {
        if (t < 0.5f) {
            return bias(t * 2.0f, gain) / 2.0f;
        } else {
            return bias(t * 2.0f - 1.0f, 1.0f - gain) / 2.0f + 0.5f;
        }
    }

    /**
     * Pulse function - creates a pulse shape
     *
     * @param t Input [0, 1]
     * @param center Center of pulse [0, 1]
     * @param width Width of pulse [0, 1]
     * @return Pulse value [0, 1]
     *
     * Use case: Creating localized effects, feature placement zones
     */
    inline float pulse(float t, float center, float width) {
        float halfWidth = width * 0.5f;
        float dist = std::abs(t - center);
        if (dist > halfWidth) return 0.0f;
        return smoothstep(halfWidth, 0.0f, dist);
    }

    /**
     * Step function with smoothing
     * Smooth version of binary threshold
     *
     * @param value Input value
     * @param threshold Threshold value
     * @param smoothing Smoothing width around threshold
     * @return Smooth step [0, 1]
     *
     * Use case: Smooth binary decisions (e.g., tree spawning probability)
     */
    inline float smoothThreshold(float value, float threshold, float smoothing) {
        return smoothstep(threshold - smoothing, threshold + smoothing, value);
    }

} // namespace BiomeInterpolation
