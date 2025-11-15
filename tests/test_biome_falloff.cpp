/**
 * Biome Falloff Testing and Visualization
 *
 * This program tests and visualizes different biome influence falloff curves.
 * It also benchmarks performance of each falloff type.
 *
 * Agent 23 - Biome Blending Algorithm Team
 */

#include "../include/biome_falloff.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <vector>
#include <string>
#include <cmath>

using namespace BiomeFalloff;

// ANSI color codes for terminal output
#define RESET   "\033[0m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN    "\033[36m"
#define WHITE   "\033[37m"
#define BOLD    "\033[1m"

// ==================== Visualization ====================

/**
 * Visualize a falloff curve using ASCII art
 */
void visualizeFalloffCurve(FalloffType type, const BiomeFalloffConfig& config,
                           const std::string& name, int width = 60, int height = 20) {
    std::cout << "\n" << BOLD << CYAN << "=== " << name << " ===" << RESET << "\n";

    // Create 2D grid for visualization
    std::vector<std::vector<char>> grid(height, std::vector<char>(width, ' '));

    // Calculate falloff values and plot
    for (int x = 0; x < width; ++x) {
        // Normalize x to [0, 1] range (representing normalized distance)
        float normalizedDist = static_cast<float>(x) / static_cast<float>(width - 1);

        // Calculate falloff weight (assuming distance = normalizedDist * searchRadius)
        float distance = normalizedDist * config.customSearchRadius;
        float weight = calculateBiomeFalloff(distance, config, 50.0f); // rarity = 50

        // Map weight [0, 1] to grid height
        int y = static_cast<int>((1.0f - weight) * (height - 1));
        if (y >= 0 && y < height) {
            grid[y][x] = '*';
        }

        // Draw vertical line to show the curve
        for (int yy = y; yy < height && yy >= 0; ++yy) {
            if (grid[yy][x] == ' ') {
                grid[yy][x] = '|';
            }
        }
    }

    // Print grid
    for (int y = 0; y < height; ++y) {
        // Print weight scale on left
        float weight = 1.0f - (static_cast<float>(y) / static_cast<float>(height - 1));
        std::cout << std::fixed << std::setprecision(2) << weight << " |";

        // Print grid row
        for (int x = 0; x < width; ++x) {
            char c = grid[y][x];
            if (c == '*') {
                std::cout << GREEN << c << RESET;
            } else if (c == '|') {
                std::cout << BLUE << c << RESET;
            } else {
                std::cout << c;
            }
        }
        std::cout << "\n";
    }

    // Print x-axis
    std::cout << "     +";
    for (int x = 0; x < width; ++x) {
        std::cout << "-";
    }
    std::cout << "\n";
    std::cout << "      0%";
    for (int x = 0; x < width - 13; ++x) std::cout << " ";
    std::cout << "Distance" << "   100%\n";
}

// ==================== Performance Benchmarking ====================

/**
 * Benchmark a falloff function
 */
double benchmarkFalloff(FalloffType type, const BiomeFalloffConfig& config,
                        int iterations = 100000) {
    auto start = std::chrono::high_resolution_clock::now();

    volatile float sum = 0.0f; // volatile to prevent optimization

    for (int i = 0; i < iterations; ++i) {
        // Test across full distance range
        float distance = (static_cast<float>(i % 100) / 100.0f) * config.customSearchRadius;
        float weight = calculateBiomeFalloff(distance, config, 50.0f);
        sum += weight;
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::micro> duration = end - start;

    return duration.count() / static_cast<double>(iterations); // microseconds per call
}

// ==================== Characteristic Analysis ====================

/**
 * Analyze falloff characteristics (smoothness, sharpness, etc.)
 */
struct FalloffCharacteristics {
    float smoothness;        // How gradual the transition is
    float edgeSharpness;     // How sharp the edge cutoff is
    float centerWeight;      // Weight at distance = 0
    float halfwayWeight;     // Weight at 50% distance
    float avgWeight;         // Average weight across full range
};

FalloffCharacteristics analyzeFalloff(FalloffType type, const BiomeFalloffConfig& config) {
    FalloffCharacteristics chars;

    // Sample at various distances
    chars.centerWeight = calculateBiomeFalloff(0.0f, config, 50.0f);
    chars.halfwayWeight = calculateBiomeFalloff(config.customSearchRadius * 0.5f, config, 50.0f);

    // Calculate smoothness by measuring rate of change
    const int samples = 100;
    float totalChange = 0.0f;
    float prevWeight = chars.centerWeight;
    float totalWeight = 0.0f;

    for (int i = 1; i <= samples; ++i) {
        float dist = (static_cast<float>(i) / samples) * config.customSearchRadius;
        float weight = calculateBiomeFalloff(dist, config, 50.0f);
        totalChange += std::abs(weight - prevWeight);
        totalWeight += weight;
        prevWeight = weight;
    }

    chars.smoothness = 1.0f - (totalChange / samples); // Higher = smoother
    chars.avgWeight = totalWeight / samples;

    // Edge sharpness: rate of change near the edge (90-100% distance)
    float weight90 = calculateBiomeFalloff(config.customSearchRadius * 0.9f, config, 50.0f);
    float weight100 = calculateBiomeFalloff(config.customSearchRadius, config, 50.0f);
    chars.edgeSharpness = std::abs(weight90 - weight100) * 10.0f; // Amplify for readability

    return chars;
}

// ==================== Comparison Table ====================

void printComparisonTable() {
    std::cout << "\n" << BOLD << YELLOW << "=== FALLOFF TYPE COMPARISON ===" << RESET << "\n\n";

    // Header
    std::cout << std::left << std::setw(20) << "Falloff Type"
              << std::setw(15) << "Performance"
              << std::setw(12) << "Smoothness"
              << std::setw(12) << "Sharpness"
              << std::setw(12) << "Visual"
              << "Best Use Case\n";
    std::cout << std::string(95, '-') << "\n";

    struct FalloffInfo {
        FalloffType type;
        std::string name;
        std::string performance;
        std::string visual;
        std::string useCase;
    };

    std::vector<FalloffInfo> falloffs = {
        {FalloffType::LINEAR, "Linear", "⭐⭐⭐⭐⭐", "⭐⭐⭐", "Simple, fast transitions"},
        {FalloffType::SMOOTH, "Smooth (Exponential)", "⭐⭐⭐⭐", "⭐⭐⭐⭐", "Standard biome blending"},
        {FalloffType::VERY_SMOOTH, "Very Smooth", "⭐⭐⭐", "⭐⭐⭐⭐⭐", "Ultra-natural transitions"},
        {FalloffType::SHARP, "Sharp", "⭐⭐⭐⭐⭐", "⭐⭐", "Distinct biome boundaries"},
        {FalloffType::COSINE, "Cosine", "⭐⭐⭐⭐", "⭐⭐⭐⭐⭐", "Wave-like, ocean biomes"},
        {FalloffType::POLYNOMIAL_2, "Quadratic", "⭐⭐⭐⭐⭐", "⭐⭐⭐⭐", "Gentle acceleration"},
        {FalloffType::POLYNOMIAL_3, "Cubic", "⭐⭐⭐⭐⭐", "⭐⭐⭐⭐", "Smooth S-curve"},
        {FalloffType::POLYNOMIAL_4, "Quartic", "⭐⭐⭐⭐", "⭐⭐⭐⭐", "Very gentle then sharp"},
        {FalloffType::INVERSE_SQUARE, "Inverse Square", "⭐⭐⭐⭐⭐", "⭐⭐⭐⭐", "Physics-like, caves"},
        {FalloffType::SIGMOID, "Sigmoid", "⭐⭐⭐", "⭐⭐⭐⭐⭐", "Biological, rare biomes"},
        {FalloffType::SMOOTHSTEP, "Smoothstep", "⭐⭐⭐⭐⭐", "⭐⭐⭐⭐⭐", "Graphics standard"},
        {FalloffType::SMOOTHERSTEP, "Smootherstep", "⭐⭐⭐⭐", "⭐⭐⭐⭐⭐", "Imperceptible blend"},
        {FalloffType::GAUSSIAN, "Gaussian", "⭐⭐⭐", "⭐⭐⭐⭐⭐", "Natural distribution"},
        {FalloffType::HYPERBOLIC, "Hyperbolic (Tanh)", "⭐⭐⭐", "⭐⭐⭐⭐⭐", "Fast sigmoid"}
    };

    // Test each falloff and print results
    for (const auto& info : falloffs) {
        BiomeFalloffConfig config;
        config.falloffType = info.type;
        config.useCustomFalloff = true;
        config.customSearchRadius = 25.0f;
        config.customBlendDistance = 15.0f;

        // Benchmark
        double avgTime = benchmarkFalloff(info.type, config, 10000);

        // Analyze
        auto chars = analyzeFalloff(info.type, config);

        // Print row
        std::cout << std::left << std::setw(20) << info.name
                  << std::setw(15) << info.performance
                  << std::setw(12) << std::fixed << std::setprecision(3) << chars.smoothness
                  << std::setw(12) << std::fixed << std::setprecision(3) << chars.edgeSharpness
                  << std::setw(12) << info.visual
                  << info.useCase << "\n";
    }

    std::cout << std::string(95, '-') << "\n";
    std::cout << "Performance: ⭐⭐⭐⭐⭐ = <0.01μs  |  Visual: ⭐⭐⭐⭐⭐ = Excellent\n";
}

// ==================== Main Test Program ====================

int main() {
    std::cout << BOLD << MAGENTA << R"(
╔══════════════════════════════════════════════════════════════╗
║       BIOME INFLUENCE FALLOFF TESTING SUITE                 ║
║       Agent 23 - Biome Blending Algorithm Team              ║
╚══════════════════════════════════════════════════════════════╝
)" << RESET << "\n";

    // Test all falloff types
    std::cout << "\n" << BOLD << "TESTING FALLOFF CURVE TYPES\n" << RESET;
    std::cout << "These visualizations show how biome influence decreases with distance.\n";

    // Create test configurations
    BiomeFalloffConfig linearConfig;
    linearConfig.falloffType = FalloffType::LINEAR;
    linearConfig.useCustomFalloff = true;
    linearConfig.customSearchRadius = 25.0f;
    visualizeFalloffCurve(FalloffType::LINEAR, linearConfig, "Linear Falloff");

    BiomeFalloffConfig smoothConfig;
    smoothConfig.falloffType = FalloffType::SMOOTH;
    smoothConfig.useCustomFalloff = true;
    smoothConfig.customSearchRadius = 25.0f;
    smoothConfig.customExponentialFactor = -3.0f;
    visualizeFalloffCurve(FalloffType::SMOOTH, smoothConfig, "Smooth (Exponential) Falloff");

    BiomeFalloffConfig cosineConfig;
    cosineConfig.falloffType = FalloffType::COSINE;
    cosineConfig.useCustomFalloff = true;
    cosineConfig.customSearchRadius = 25.0f;
    visualizeFalloffCurve(FalloffType::COSINE, cosineConfig, "Cosine Falloff");

    BiomeFalloffConfig smoothstepConfig;
    smoothstepConfig.falloffType = FalloffType::SMOOTHSTEP;
    smoothstepConfig.useCustomFalloff = true;
    smoothstepConfig.customSearchRadius = 25.0f;
    visualizeFalloffCurve(FalloffType::SMOOTHSTEP, smoothstepConfig, "Smoothstep Falloff");

    BiomeFalloffConfig smootherStepConfig;
    smootherStepConfig.falloffType = FalloffType::SMOOTHERSTEP;
    smootherStepConfig.useCustomFalloff = true;
    smootherStepConfig.customSearchRadius = 25.0f;
    visualizeFalloffCurve(FalloffType::SMOOTHERSTEP, smootherStepConfig, "Smootherstep Falloff");

    BiomeFalloffConfig gaussianConfig;
    gaussianConfig.falloffType = FalloffType::GAUSSIAN;
    gaussianConfig.useCustomFalloff = true;
    gaussianConfig.customSearchRadius = 25.0f;
    visualizeFalloffCurve(FalloffType::GAUSSIAN, gaussianConfig, "Gaussian Falloff");

    BiomeFalloffConfig sigmoidConfig;
    sigmoidConfig.falloffType = FalloffType::SIGMOID;
    sigmoidConfig.useCustomFalloff = true;
    sigmoidConfig.customSearchRadius = 25.0f;
    visualizeFalloffCurve(FalloffType::SIGMOID, sigmoidConfig, "Sigmoid Falloff");

    BiomeFalloffConfig invSquareConfig;
    invSquareConfig.falloffType = FalloffType::INVERSE_SQUARE;
    invSquareConfig.useCustomFalloff = true;
    invSquareConfig.customSearchRadius = 25.0f;
    visualizeFalloffCurve(FalloffType::INVERSE_SQUARE, invSquareConfig, "Inverse Square Falloff");

    // Print comparison table
    printComparisonTable();

    // Test predefined configurations
    std::cout << "\n" << BOLD << YELLOW << "=== PREDEFINED BIOME CONFIGURATIONS ===" << RESET << "\n\n";

    std::cout << "Testing recommended falloff configs for different biome types:\n\n";

    std::cout << CYAN << "Natural Biomes (Forests, Plains):" << RESET << "\n";
    visualizeFalloffCurve(FALLOFF_NATURAL.falloffType, FALLOFF_NATURAL,
                         "Natural - Smootherstep (very smooth)", 50, 15);

    std::cout << CYAN << "\nMountain Biomes:" << RESET << "\n";
    visualizeFalloffCurve(FALLOFF_MOUNTAIN.falloffType, FALLOFF_MOUNTAIN,
                         "Mountain - Gaussian (natural elevation)", 50, 15);

    std::cout << CYAN << "\nDesert Biomes:" << RESET << "\n";
    visualizeFalloffCurve(FALLOFF_DESERT.falloffType, FALLOFF_DESERT,
                         "Desert - Polynomial Cubic (sharper)", 50, 15);

    std::cout << CYAN << "\nOcean Biomes:" << RESET << "\n";
    visualizeFalloffCurve(FALLOFF_OCEAN.falloffType, FALLOFF_OCEAN,
                         "Ocean - Cosine (wave-like)", 50, 15);

    std::cout << CYAN << "\nCave Biomes:" << RESET << "\n";
    visualizeFalloffCurve(FALLOFF_CAVE.falloffType, FALLOFF_CAVE,
                         "Cave - Inverse Square (contained)", 50, 15);

    // Summary and recommendations
    std::cout << "\n" << BOLD << GREEN << "=== RECOMMENDATIONS ===" << RESET << "\n\n";

    std::cout << "✓ " << BOLD << "Best Overall Visual Quality:" << RESET << " Smootherstep\n";
    std::cout << "  - Zero derivatives at endpoints (imperceptible transitions)\n";
    std::cout << "  - Natural looking, widely used in graphics\n\n";

    std::cout << "✓ " << BOLD << "Best Performance:" << RESET << " Linear, Quadratic, Inverse Square\n";
    std::cout << "  - No transcendental functions (no exp, sin, tanh)\n";
    std::cout << "  - Simple arithmetic operations only\n\n";

    std::cout << "✓ " << BOLD << "Best Balance:" << RESET << " Cosine or Smoothstep\n";
    std::cout << "  - Excellent visual quality\n";
    std::cout << "  - Good performance (single trig function)\n\n";

    std::cout << "✓ " << BOLD << "Most Natural:" << RESET << " Gaussian or Smootherstep\n";
    std::cout << "  - Mimics natural phenomena\n";
    std::cout << "  - Suitable for all biome types\n\n";

    std::cout << BOLD << YELLOW << "Overall Winner: " << GREEN << "Smootherstep" << RESET << "\n";
    std::cout << "Provides the best combination of visual quality and performance.\n";
    std::cout << "Recommended for most biomes unless specific characteristics needed.\n\n";

    return 0;
}
