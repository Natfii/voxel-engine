/**
 * Test Suite for Biome Interpolation Utilities
 *
 * Validates all interpolation functions for correctness, edge cases, and expected behavior.
 */

#include "../include/biome_interpolation.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <cassert>
#include <vector>

using namespace BiomeInterpolation;

// Test result tracking
struct TestResults {
    int passed = 0;
    int failed = 0;

    void reportTest(const std::string& testName, bool result) {
        if (result) {
            passed++;
            std::cout << "[PASS] " << testName << std::endl;
        } else {
            failed++;
            std::cout << "[FAIL] " << testName << std::endl;
        }
    }

    void printSummary() const {
        std::cout << "\n==================== TEST SUMMARY ====================" << std::endl;
        std::cout << "Passed: " << passed << std::endl;
        std::cout << "Failed: " << failed << std::endl;
        std::cout << "Total:  " << (passed + failed) << std::endl;
        std::cout << "=====================================================" << std::endl;
    }
};

// Helper: approximate float equality
bool floatEqual(float a, float b, float epsilon = 0.0001f) {
    return std::abs(a - b) < epsilon;
}

// Helper: approximate vec3 equality
bool vec3Equal(const glm::vec3& a, const glm::vec3& b, float epsilon = 0.0001f) {
    return floatEqual(a.r, b.r, epsilon) &&
           floatEqual(a.g, b.g, epsilon) &&
           floatEqual(a.b, b.b, epsilon);
}

// ==================== Basic Interpolation Tests ====================

void testBasicInterpolation(TestResults& results) {
    std::cout << "\n=== Testing Basic Interpolation ===" << std::endl;

    // Test lerp
    results.reportTest("lerp(0, 100, 0.0) == 0", floatEqual(lerp(0.0f, 100.0f, 0.0f), 0.0f));
    results.reportTest("lerp(0, 100, 1.0) == 100", floatEqual(lerp(0.0f, 100.0f, 1.0f), 100.0f));
    results.reportTest("lerp(0, 100, 0.5) == 50", floatEqual(lerp(0.0f, 100.0f, 0.5f), 50.0f));
    results.reportTest("lerp(20, 80, 0.25) == 35", floatEqual(lerp(20.0f, 80.0f, 0.25f), 35.0f));

    // Test lerpClamped
    results.reportTest("lerpClamped(0, 100, -0.5) == 0", floatEqual(lerpClamped(0.0f, 100.0f, -0.5f), 0.0f));
    results.reportTest("lerpClamped(0, 100, 1.5) == 100", floatEqual(lerpClamped(0.0f, 100.0f, 1.5f), 100.0f));

    // Test inverseLerp
    results.reportTest("inverseLerp(0, 100, 50) == 0.5", floatEqual(inverseLerp(0.0f, 100.0f, 50.0f), 0.5f));
    results.reportTest("inverseLerp(20, 80, 50) == 0.5", floatEqual(inverseLerp(20.0f, 80.0f, 50.0f), 0.5f));
    results.reportTest("inverseLerp(0, 100, 0) == 0.0", floatEqual(inverseLerp(0.0f, 100.0f, 0.0f), 0.0f));

    // Test smoothstep
    float s0 = smoothstep(0.0f, 1.0f, 0.0f);
    float s1 = smoothstep(0.0f, 1.0f, 1.0f);
    float s05 = smoothstep(0.0f, 1.0f, 0.5f);
    results.reportTest("smoothstep(0,1,0) == 0", floatEqual(s0, 0.0f));
    results.reportTest("smoothstep(0,1,1) == 1", floatEqual(s1, 1.0f));
    results.reportTest("smoothstep(0,1,0.5) == 0.5", floatEqual(s05, 0.5f));
    results.reportTest("smoothstep is smooth (middle > linear)", s05 >= 0.5f);

    // Test smootherstep
    float ss05 = smootherstep(0.0f, 1.0f, 0.5f);
    results.reportTest("smootherstep(0,1,0) == 0", floatEqual(smootherstep(0.0f, 1.0f, 0.0f), 0.0f));
    results.reportTest("smootherstep(0,1,1) == 1", floatEqual(smootherstep(0.0f, 1.0f, 1.0f), 1.0f));
    results.reportTest("smootherstep(0,1,0.5) == 0.5", floatEqual(ss05, 0.5f));

    // Test cosineInterp
    results.reportTest("cosineInterp(0,100,0) == 0", floatEqual(cosineInterp(0.0f, 100.0f, 0.0f), 0.0f));
    results.reportTest("cosineInterp(0,100,1) == 100", floatEqual(cosineInterp(0.0f, 100.0f, 1.0f), 100.0f));
}

// ==================== Easing Function Tests ====================

void testEasingFunctions(TestResults& results) {
    std::cout << "\n=== Testing Easing Functions ===" << std::endl;

    // Test easeInCubic
    results.reportTest("easeInCubic(0) == 0", floatEqual(easeInCubic(0.0f), 0.0f));
    results.reportTest("easeInCubic(1) == 1", floatEqual(easeInCubic(1.0f), 1.0f));
    float eic05 = easeInCubic(0.5f);
    results.reportTest("easeInCubic(0.5) < 0.5 (slow start)", eic05 < 0.5f);

    // Test easeOutCubic
    results.reportTest("easeOutCubic(0) == 0", floatEqual(easeOutCubic(0.0f), 0.0f));
    results.reportTest("easeOutCubic(1) == 1", floatEqual(easeOutCubic(1.0f), 1.0f));
    float eoc05 = easeOutCubic(0.5f);
    results.reportTest("easeOutCubic(0.5) > 0.5 (fast start)", eoc05 > 0.5f);

    // Test easeInOutCubic
    results.reportTest("easeInOutCubic(0) == 0", floatEqual(easeInOutCubic(0.0f), 0.0f));
    results.reportTest("easeInOutCubic(1) == 1", floatEqual(easeInOutCubic(1.0f), 1.0f));
    results.reportTest("easeInOutCubic(0.5) == 0.5", floatEqual(easeInOutCubic(0.5f), 0.5f));

    // Test easeInExpo
    results.reportTest("easeInExpo(0) == 0", floatEqual(easeInExpo(0.0f), 0.0f));
    results.reportTest("easeInExpo(1) == 1", floatEqual(easeInExpo(1.0f), 1.0f));

    // Test easeOutExpo
    results.reportTest("easeOutExpo(0) == 0", floatEqual(easeOutExpo(0.0f), 0.0f));
    results.reportTest("easeOutExpo(1) == 1", floatEqual(easeOutExpo(1.0f), 1.0f));

    // Test easeInCirc
    results.reportTest("easeInCirc(0) == 0", floatEqual(easeInCirc(0.0f), 0.0f));
    results.reportTest("easeInCirc(1) == 1", floatEqual(easeInCirc(1.0f), 1.0f));

    // Test easeOutCirc
    results.reportTest("easeOutCirc(0) == 0", floatEqual(easeOutCirc(0.0f), 0.0f));
    results.reportTest("easeOutCirc(1) == 1", floatEqual(easeOutCirc(1.0f), 1.0f));
}

// ==================== Weighted Interpolation Tests ====================

void testWeightedInterpolation(TestResults& results) {
    std::cout << "\n=== Testing Weighted Interpolation ===" << std::endl;

    // Test weightedAverage with arrays
    {
        float values[] = {100.0f, 80.0f, 60.0f};
        float weights[] = {0.5f, 0.3f, 0.2f};
        float result = weightedAverage(values, weights, 3, false);
        float expected = 100.0f * 0.5f + 80.0f * 0.3f + 60.0f * 0.2f; // = 86
        results.reportTest("weightedAverage([100,80,60], [0.5,0.3,0.2]) == 86",
                          floatEqual(result, expected));
    }

    // Test with normalization
    {
        float values[] = {100.0f, 50.0f};
        float weights[] = {2.0f, 2.0f}; // Not normalized
        float result = weightedAverage(values, weights, 2, true);
        float expected = 75.0f; // (100 + 50) / 2
        results.reportTest("weightedAverage with normalization", floatEqual(result, expected));
    }

    // Test with vectors
    {
        std::vector<float> values = {0.0f, 100.0f};
        std::vector<float> weights = {0.3f, 0.7f};
        float result = weightedAverage(values, weights, false);
        float expected = 70.0f;
        results.reportTest("weightedAverage (vector version)", floatEqual(result, expected));
    }

    // Test weightedAverageInt
    {
        int values[] = {100, 80, 60};
        float weights[] = {0.5f, 0.3f, 0.2f};
        float result = weightedAverageInt(values, weights, 3, false);
        float expected = 86.0f;
        results.reportTest("weightedAverageInt([100,80,60], [0.5,0.3,0.2])",
                          floatEqual(result, expected));
    }

    // Test normalizeWeights
    {
        float weights[] = {2.0f, 3.0f, 5.0f}; // Sum = 10
        normalizeWeights(weights, 3);
        float sum = weights[0] + weights[1] + weights[2];
        results.reportTest("normalizeWeights sums to 1.0", floatEqual(sum, 1.0f));
        results.reportTest("normalizeWeights preserves ratios",
                          floatEqual(weights[0], 0.2f) &&
                          floatEqual(weights[1], 0.3f) &&
                          floatEqual(weights[2], 0.5f));
    }

    // Edge case: single value
    {
        float values[] = {42.0f};
        float weights[] = {1.0f};
        float result = weightedAverage(values, weights, 1, false);
        results.reportTest("weightedAverage with single value", floatEqual(result, 42.0f));
    }
}

// ==================== Color Blending Tests ====================

void testColorBlending(TestResults& results) {
    std::cout << "\n=== Testing Color Blending ===" << std::endl;

    // Test lerpColor
    {
        glm::vec3 red(1.0f, 0.0f, 0.0f);
        glm::vec3 blue(0.0f, 0.0f, 1.0f);
        glm::vec3 result = lerpColor(red, blue, 0.5f);
        glm::vec3 expected(0.5f, 0.0f, 0.5f); // Purple
        results.reportTest("lerpColor(red, blue, 0.5) == purple", vec3Equal(result, expected));
    }

    // Test lerpColor at edges
    {
        glm::vec3 red(1.0f, 0.0f, 0.0f);
        glm::vec3 blue(0.0f, 0.0f, 1.0f);
        results.reportTest("lerpColor(red, blue, 0.0) == red", vec3Equal(lerpColor(red, blue, 0.0f), red));
        results.reportTest("lerpColor(red, blue, 1.0) == blue", vec3Equal(lerpColor(red, blue, 1.0f), blue));
    }

    // Test smoothColorBlend
    {
        glm::vec3 c1(0.0f, 0.0f, 0.0f);
        glm::vec3 c2(1.0f, 1.0f, 1.0f);
        glm::vec3 result = smoothColorBlend(c1, c2, 0.5f);
        results.reportTest("smoothColorBlend produces smooth result",
                          result.r > 0.4f && result.r < 0.6f);
    }

    // Test weightedColorAverage
    {
        glm::vec3 colors[] = {
            glm::vec3(1.0f, 0.0f, 0.0f), // Red
            glm::vec3(0.0f, 1.0f, 0.0f), // Green
            glm::vec3(0.0f, 0.0f, 1.0f)  // Blue
        };
        float weights[] = {0.5f, 0.3f, 0.2f};
        glm::vec3 result = weightedColorAverage(colors, weights, 3, false);
        glm::vec3 expected(0.5f, 0.3f, 0.2f);
        results.reportTest("weightedColorAverage blends RGB correctly",
                          vec3Equal(result, expected));
    }

    // Test HSV conversions
    {
        glm::vec3 red(1.0f, 0.0f, 0.0f);
        glm::vec3 hsv = rgbToHsv(red);
        results.reportTest("rgbToHsv(red) has hue ~0", floatEqual(hsv.x, 0.0f, 1.0f));
        results.reportTest("rgbToHsv(red) has saturation 1", floatEqual(hsv.y, 1.0f));
        results.reportTest("rgbToHsv(red) has value 1", floatEqual(hsv.z, 1.0f));

        glm::vec3 backToRgb = hsvToRgb(hsv.x, hsv.y, hsv.z);
        results.reportTest("HSV round-trip conversion", vec3Equal(red, backToRgb));
    }

    // Test HSV color lerp
    {
        glm::vec3 red(1.0f, 0.0f, 0.0f);
        glm::vec3 yellow(1.0f, 1.0f, 0.0f);
        glm::vec3 result = lerpColorHSV(red, yellow, 0.5f);
        // Should be orange-ish (hue between 0 and 60)
        glm::vec3 hsv = rgbToHsv(result);
        results.reportTest("lerpColorHSV(red, yellow) produces orange",
                          hsv.x > 10.0f && hsv.x < 50.0f);
    }
}

// ==================== Noise Variation Tests ====================

void testNoiseVariation(TestResults& results) {
    std::cout << "\n=== Testing Noise Variation ===" << std::endl;

    // Test applyNoiseVariation
    {
        float base = 100.0f;
        float result1 = applyNoiseVariation(base, 0.0f, 0.2f);
        results.reportTest("applyNoiseVariation with 0 noise == base",
                          floatEqual(result1, base));

        float result2 = applyNoiseVariation(base, 0.5f, 0.2f);
        results.reportTest("applyNoiseVariation increases with positive noise",
                          result2 > base);

        float result3 = applyNoiseVariation(base, -0.5f, 0.2f);
        results.reportTest("applyNoiseVariation decreases with negative noise",
                          result3 < base);
    }

    // Test applyAsymmetricVariation
    {
        float base = 100.0f;
        float result1 = applyAsymmetricVariation(base, 0.5f, 0.2f, 0.3f);
        results.reportTest("applyAsymmetricVariation at midpoint == base",
                          floatEqual(result1, base));

        float result2 = applyAsymmetricVariation(base, 1.0f, 0.2f, 0.3f);
        results.reportTest("applyAsymmetricVariation at max increases",
                          result2 > base && result2 <= base * 1.2f);

        float result3 = applyAsymmetricVariation(base, 0.0f, 0.2f, 0.3f);
        results.reportTest("applyAsymmetricVariation at min decreases",
                          result3 < base && result3 >= base * 0.7f);
    }

    // Test createVariationHotspot
    {
        float base = 50.0f;
        float variation = 100.0f;

        float result1 = createVariationHotspot(base, 0.3f, 0.5f, variation);
        results.reportTest("createVariationHotspot below threshold == base",
                          floatEqual(result1, base));

        float result2 = createVariationHotspot(base, 1.0f, 0.5f, variation);
        results.reportTest("createVariationHotspot at max approaches variation",
                          result2 > 90.0f);
    }

    // Test turbulence
    {
        float noiseValues[] = {0.5f, 0.3f, 0.2f, 0.1f};
        float result = turbulence(noiseValues, 4, 0.5f);
        results.reportTest("turbulence returns normalized value",
                          result >= 0.0f && result <= 1.0f);
    }

    // Test ridgedNoise
    {
        float result1 = ridgedNoise(0.0f, 1.0f);
        results.reportTest("ridgedNoise(0) == 1", floatEqual(result1, 1.0f));

        float result2 = ridgedNoise(1.0f, 1.0f);
        results.reportTest("ridgedNoise(1) == 0", floatEqual(result2, 0.0f));

        float result3 = ridgedNoise(-1.0f, 1.0f);
        results.reportTest("ridgedNoise(-1) == 0", floatEqual(result3, 0.0f));
    }
}

// ==================== Utility Function Tests ====================

void testUtilityFunctions(TestResults& results) {
    std::cout << "\n=== Testing Utility Functions ===" << std::endl;

    // Test remap
    {
        float result = remap(0.0f, -1.0f, 1.0f, 0.0f, 100.0f);
        results.reportTest("remap(0, [-1,1], [0,100]) == 50", floatEqual(result, 50.0f));

        float result2 = remap(-1.0f, -1.0f, 1.0f, 0.0f, 100.0f);
        results.reportTest("remap(-1, [-1,1], [0,100]) == 0", floatEqual(result2, 0.0f));

        float result3 = remap(1.0f, -1.0f, 1.0f, 0.0f, 100.0f);
        results.reportTest("remap(1, [-1,1], [0,100]) == 100", floatEqual(result3, 100.0f));
    }

    // Test remapClamped
    {
        float result = remapClamped(2.0f, -1.0f, 1.0f, 0.0f, 100.0f);
        results.reportTest("remapClamped clamps overflow", floatEqual(result, 100.0f));

        float result2 = remapClamped(-2.0f, -1.0f, 1.0f, 0.0f, 100.0f);
        results.reportTest("remapClamped clamps underflow", floatEqual(result2, 0.0f));
    }

    // Test bias
    {
        results.reportTest("bias(0.5, 0.5) == 0.5", floatEqual(bias(0.5f, 0.5f), 0.5f));
        results.reportTest("bias(0.5, 0.7) > 0.5", bias(0.5f, 0.7f) > 0.5f);
        results.reportTest("bias(0.5, 0.3) < 0.5", bias(0.5f, 0.3f) < 0.5f);
    }

    // Test gain
    {
        results.reportTest("gain(0.5, 0.5) == 0.5", floatEqual(gain(0.5f, 0.5f), 0.5f));
        results.reportTest("gain(0, any) == 0", floatEqual(gain(0.0f, 0.7f), 0.0f));
        results.reportTest("gain(1, any) == 1", floatEqual(gain(1.0f, 0.7f), 1.0f));
    }

    // Test pulse
    {
        float result1 = pulse(0.5f, 0.5f, 0.2f);
        results.reportTest("pulse at center == 1", floatEqual(result1, 1.0f));

        float result2 = pulse(0.3f, 0.5f, 0.2f);
        results.reportTest("pulse near center > 0", result2 > 0.0f);

        float result3 = pulse(0.0f, 0.5f, 0.2f);
        results.reportTest("pulse far from center == 0", floatEqual(result3, 0.0f));
    }

    // Test smoothThreshold
    {
        float result1 = smoothThreshold(10.0f, 10.0f, 0.0f);
        results.reportTest("smoothThreshold at exact threshold", result1 >= 0.4f && result1 <= 0.6f);

        float result2 = smoothThreshold(15.0f, 10.0f, 2.0f);
        results.reportTest("smoothThreshold above threshold ~1", result2 > 0.9f);

        float result3 = smoothThreshold(5.0f, 10.0f, 2.0f);
        results.reportTest("smoothThreshold below threshold ~0", result3 < 0.1f);
    }
}

// ==================== Integration Tests ====================

void testRealWorldScenarios(TestResults& results) {
    std::cout << "\n=== Testing Real-World Scenarios ===" << std::endl;

    // Scenario: Blending tree density from 3 biomes
    {
        int densities[] = {70, 50, 30}; // Forest, Plains, Desert
        float weights[] = {0.5f, 0.3f, 0.2f};
        float blended = weightedAverageInt(densities, weights, 3, false);
        float expected = 70.0f * 0.5f + 50.0f * 0.3f + 30.0f * 0.2f; // = 56
        results.reportTest("Blend tree density from 3 biomes", floatEqual(blended, expected));
    }

    // Scenario: Smooth biome boundary transition
    {
        float forestHeight = 100.0f;
        float plainsHeight = 65.0f;
        float distance = 0.3f; // 30% into transition
        float smoothDist = smoothstep(0.0f, 1.0f, distance);
        float height = lerp(forestHeight, plainsHeight, smoothDist);
        results.reportTest("Smooth height transition", height > plainsHeight && height < forestHeight);
    }

    // Scenario: Color gradient for fog transition
    {
        glm::vec3 forestFog(0.5f, 0.7f, 0.9f);  // Blue-ish
        glm::vec3 desertFog(0.9f, 0.8f, 0.6f);  // Yellow-ish
        glm::vec3 blended = smoothColorBlend(forestFog, desertFog, 0.5f);
        results.reportTest("Fog color blending produces valid color",
                          blended.r >= 0.0f && blended.r <= 1.0f &&
                          blended.g >= 0.0f && blended.g <= 1.0f &&
                          blended.b >= 0.0f && blended.b <= 1.0f);
    }

    // Scenario: Noise-based tree density variation
    {
        float baseDensity = 50.0f;
        float noise = 0.3f; // Some noise value
        float varied = applyNoiseVariation(baseDensity, noise, 0.2f);
        results.reportTest("Tree density variation stays reasonable",
                          varied > baseDensity * 0.7f && varied < baseDensity * 1.3f);
    }
}

// ==================== Main Test Runner ====================

int main() {
    std::cout << "======================================" << std::endl;
    std::cout << "Biome Interpolation Utilities Test Suite" << std::endl;
    std::cout << "======================================" << std::endl;

    TestResults results;

    testBasicInterpolation(results);
    testEasingFunctions(results);
    testWeightedInterpolation(results);
    testColorBlending(results);
    testNoiseVariation(results);
    testUtilityFunctions(results);
    testRealWorldScenarios(results);

    results.printSummary();

    return (results.failed == 0) ? 0 : 1;
}
