#include "../include/biome_map.h"
#include "../include/biome_system.h"
#include <iostream>
#include <cassert>
#include <cmath>

// Test helper function
void assertRange(float value, float min, float max, const std::string& name) {
    if (value < min || value > max) {
        std::cerr << "FAIL: " << name << " = " << value << " (expected " << min << "-" << max << ")" << std::endl;
        assert(false);
    }
}

void testPresetConfigurations() {
    std::cout << "\n=== Testing Preset Configurations ===" << std::endl;

    const int seed = 12345;

    // Test Continental preset (default)
    {
        BiomeNoise::BiomeNoiseConfig config = BiomeNoise::createContinentalConfig();
        std::cout << "Testing: " << config.configName << std::endl;

        assert(config.configName == "Continental Scale");
        assert(config.temperature.baseLayer.frequency < 0.001f);  // Very low frequency
        assert(config.moisture.baseLayer.frequency < 0.001f);

        BiomeMap map(seed, config);

        // Sample noise at various positions
        float temp1 = map.getTemperatureAt(0, 0);
        float temp2 = map.getTemperatureAt(1000, 1000);
        assertRange(temp1, 0.0f, 100.0f, "Continental temperature");
        assertRange(temp2, 0.0f, 100.0f, "Continental temperature");

        std::cout << "  Temperature range check: PASS" << std::endl;
    }

    // Test Regional preset
    {
        BiomeNoise::BiomeNoiseConfig config = BiomeNoise::createRegionalConfig();
        std::cout << "Testing: " << config.configName << std::endl;

        assert(config.configName == "Regional Scale");
        BiomeMap map(seed, config);

        float moisture = map.getMoistureAt(500, 500);
        assertRange(moisture, 0.0f, 100.0f, "Regional moisture");

        std::cout << "  Moisture range check: PASS" << std::endl;
    }

    // Test Local preset
    {
        BiomeNoise::BiomeNoiseConfig config = BiomeNoise::createLocalConfig();
        std::cout << "Testing: " << config.configName << std::endl;

        BiomeMap map(seed, config);

        float weirdness = map.getWeirdnessAt(250, 250);
        assertRange(weirdness, 0.0f, 100.0f, "Local weirdness");

        std::cout << "  Weirdness range check: PASS" << std::endl;
    }

    // Test Compact preset
    {
        BiomeNoise::BiomeNoiseConfig config = BiomeNoise::createCompactConfig();
        std::cout << "Testing: " << config.configName << std::endl;

        BiomeMap map(seed, config);

        float erosion = map.getErosionAt(100, 100);
        assertRange(erosion, 0.0f, 100.0f, "Compact erosion");

        std::cout << "  Erosion range check: PASS" << std::endl;
    }

    std::cout << "Preset configurations: ALL TESTS PASSED" << std::endl;
}

void testCustomConfiguration() {
    std::cout << "\n=== Testing Custom Configuration ===" << std::endl;

    const int seed = 54321;

    // Create custom configuration
    BiomeNoise::BiomeNoiseConfig config;
    config.configName = "Test Custom";

    // Configure temperature with extreme settings
    config.temperature.baseLayer.frequency = 0.0001f;  // Extra wide
    config.temperature.detailLayer.frequency = 0.01f;  // High detail
    config.temperature.blendRatio = 0.5f;  // Equal blend

    // Configure moisture normally
    config.moisture = BiomeNoise::createContinentalConfig().moisture;

    // Configure weirdness and erosion
    config.weirdness = BiomeNoise::createContinentalConfig().weirdness;
    config.erosion = BiomeNoise::createContinentalConfig().erosion;

    BiomeMap map(seed, config);

    // Verify configuration was applied
    const auto& appliedConfig = map.getNoiseConfig();
    assert(appliedConfig.configName == "Test Custom");
    assert(std::abs(appliedConfig.temperature.baseLayer.frequency - 0.0001f) < 0.00001f);
    assert(std::abs(appliedConfig.temperature.blendRatio - 0.5f) < 0.01f);

    // Test noise sampling
    float temp = map.getTemperatureAt(0, 0);
    assertRange(temp, 0.0f, 100.0f, "Custom config temperature");

    std::cout << "Custom configuration: ALL TESTS PASSED" << std::endl;
}

void testLayerModification() {
    std::cout << "\n=== Testing Layer-Level Modification ===" << std::endl;

    const int seed = 99999;
    BiomeMap map(seed);  // Default continental

    // Modify temperature base layer only
    BiomeNoise::NoiseLayerConfig customLayer;
    customLayer.frequency = 0.005f;  // Much higher than default
    customLayer.octaves = 3;
    customLayer.noiseType = FastNoiseLite::NoiseType_Perlin;

    std::cout << "Modifying temperature base layer..." << std::endl;
    map.setLayerConfig(0, true, customLayer);  // Dimension 0 (temp), base layer

    // Verify the change
    const auto& config = map.getNoiseConfig();
    assert(std::abs(config.temperature.baseLayer.frequency - 0.005f) < 0.00001f);
    assert(config.temperature.baseLayer.octaves == 3);

    // Test that noise still works
    float temp = map.getTemperatureAt(100, 100);
    assertRange(temp, 0.0f, 100.0f, "Modified layer temperature");

    std::cout << "Layer modification: ALL TESTS PASSED" << std::endl;
}

void testDimensionModification() {
    std::cout << "\n=== Testing Dimension-Level Modification ===" << std::endl;

    const int seed = 11111;
    BiomeMap map(seed);

    // Create custom moisture dimension
    BiomeNoise::DimensionConfig moistConfig;
    moistConfig.dimensionName = "Custom Moisture";
    moistConfig.baseLayer.frequency = 0.002f;
    moistConfig.detailLayer.frequency = 0.02f;
    moistConfig.blendRatio = 0.6f;

    std::cout << "Modifying moisture dimension..." << std::endl;
    map.setDimensionConfig(1, moistConfig);  // Dimension 1 = moisture

    // Verify the change
    const auto& config = map.getNoiseConfig();
    assert(std::abs(config.moisture.blendRatio - 0.6f) < 0.01f);

    // Test that noise still works
    float moisture = map.getMoistureAt(200, 200);
    assertRange(moisture, 0.0f, 100.0f, "Modified dimension moisture");

    std::cout << "Dimension modification: ALL TESTS PASSED" << std::endl;
}

void testPresetSwitching() {
    std::cout << "\n=== Testing Runtime Preset Switching ===" << std::endl;

    const int seed = 77777;
    BiomeMap map(seed);

    // Test switching between presets
    std::cout << "Switching to Regional preset..." << std::endl;
    map.applyPreset("regional");
    assert(map.getNoiseConfig().configName == "Regional Scale");

    std::cout << "Switching to Compact preset..." << std::endl;
    map.applyPreset("compact");
    assert(map.getNoiseConfig().configName == "Compact Scale");

    std::cout << "Switching to Local preset..." << std::endl;
    map.applyPreset("local");
    assert(map.getNoiseConfig().configName == "Local Scale");

    std::cout << "Switching back to Continental preset..." << std::endl;
    map.applyPreset("continental");
    assert(map.getNoiseConfig().configName == "Continental Scale");

    // Verify noise still works after switches
    float temp = map.getTemperatureAt(300, 300);
    float moisture = map.getMoistureAt(300, 300);
    float weirdness = map.getWeirdnessAt(300, 300);
    float erosion = map.getErosionAt(300, 300);

    assertRange(temp, 0.0f, 100.0f, "Post-switch temperature");
    assertRange(moisture, 0.0f, 100.0f, "Post-switch moisture");
    assertRange(weirdness, 0.0f, 100.0f, "Post-switch weirdness");
    assertRange(erosion, 0.0f, 100.0f, "Post-switch erosion");

    std::cout << "Preset switching: ALL TESTS PASSED" << std::endl;
}

void testNoiseVariety() {
    std::cout << "\n=== Testing Noise Variety ===" << std::endl;

    const int seed = 33333;
    BiomeMap map(seed, BiomeNoise::createContinentalConfig());

    // Sample multiple positions and ensure variety
    std::vector<float> temps;
    std::vector<float> moistures;

    for (int i = 0; i < 10; i++) {
        float x = i * 1000.0f;
        float z = i * 1000.0f;
        temps.push_back(map.getTemperatureAt(x, z));
        moistures.push_back(map.getMoistureAt(x, z));
    }

    // Calculate variance to ensure noise isn't constant
    float tempSum = 0.0f, moistSum = 0.0f;
    for (size_t i = 0; i < temps.size(); i++) {
        tempSum += temps[i];
        moistSum += moistures[i];
    }
    float tempAvg = tempSum / temps.size();
    float moistAvg = moistSum / moistures.size();

    float tempVariance = 0.0f, moistVariance = 0.0f;
    for (size_t i = 0; i < temps.size(); i++) {
        tempVariance += (temps[i] - tempAvg) * (temps[i] - tempAvg);
        moistVariance += (moistures[i] - moistAvg) * (moistures[i] - moistAvg);
    }
    tempVariance /= temps.size();
    moistVariance /= moistures.size();

    std::cout << "  Temperature variance: " << tempVariance << std::endl;
    std::cout << "  Moisture variance: " << moistVariance << std::endl;

    // Variance should be > 0 (not all same values)
    assert(tempVariance > 0.1f);
    assert(moistVariance > 0.1f);

    std::cout << "Noise variety: ALL TESTS PASSED" << std::endl;
}

void testAllDimensions() {
    std::cout << "\n=== Testing All Four Dimensions ===" << std::endl;

    const int seed = 55555;
    BiomeMap map(seed);

    float x = 500.0f, z = 500.0f;

    float temp = map.getTemperatureAt(x, z);
    float moisture = map.getMoistureAt(x, z);
    float weirdness = map.getWeirdnessAt(x, z);
    float erosion = map.getErosionAt(x, z);

    std::cout << "  Position (" << x << ", " << z << "):" << std::endl;
    std::cout << "    Temperature: " << temp << std::endl;
    std::cout << "    Moisture: " << moisture << std::endl;
    std::cout << "    Weirdness: " << weirdness << std::endl;
    std::cout << "    Erosion: " << erosion << std::endl;

    assertRange(temp, 0.0f, 100.0f, "Temperature");
    assertRange(moisture, 0.0f, 100.0f, "Moisture");
    assertRange(weirdness, 0.0f, 100.0f, "Weirdness");
    assertRange(erosion, 0.0f, 100.0f, "Erosion");

    std::cout << "All dimensions: ALL TESTS PASSED" << std::endl;
}

int main() {
    std::cout << "====================================" << std::endl;
    std::cout << "Multi-Layer Biome Noise System Tests" << std::endl;
    std::cout << "====================================" << std::endl;

    try {
        testPresetConfigurations();
        testCustomConfiguration();
        testLayerModification();
        testDimensionModification();
        testPresetSwitching();
        testNoiseVariety();
        testAllDimensions();

        std::cout << "\n====================================" << std::endl;
        std::cout << "ALL TESTS PASSED SUCCESSFULLY!" << std::endl;
        std::cout << "====================================" << std::endl;

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nTEST FAILED: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "\nTEST FAILED: Unknown exception" << std::endl;
        return 1;
    }
}
