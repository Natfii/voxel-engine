#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>

/**
 * @brief Emitter shape types (where particles spawn)
 */
enum class EmitterShape {
    POINT,
    CONE,
    BOX,
    CIRCLE
};

/**
 * @brief Particle render shapes (how particles look)
 */
enum class ParticleRenderShape {
    CIRCLE,
    SQUARE,
    TRIANGLE,
    STAR,
    RING,
    SPARK
};

/**
 * @brief Blend modes for particles
 */
enum class ParticleBlendMode {
    ALPHA,
    ADDITIVE,
    PREMULTIPLIED
};

/**
 * @brief Range value with min and max
 */
struct RangeValue {
    float min = 0.0f;
    float max = 0.0f;

    RangeValue() = default;
    RangeValue(float v) : min(v), max(v) {}
    RangeValue(float mn, float mx) : min(mn), max(mx) {}

    float random() const;  // Returns random value between min and max
};

/**
 * @brief Curve key for property animation
 */
struct CurveKey {
    float time = 0.0f;   // 0-1 normalized time
    float value = 0.0f;
};

/**
 * @brief Color gradient stop
 */
struct ColorStop {
    float time = 0.0f;
    glm::vec4 color = glm::vec4(1.0f);
};

/**
 * @brief Burst configuration
 */
struct BurstConfig {
    int count = 0;
    int cycles = 1;
    float interval = 0.0f;  // Time between cycles
};

/**
 * @brief Texture configuration for particles
 */
struct ParticleTextureConfig {
    std::string atlasPath;
    int frameIndex = 0;
    int frameCount = 1;   // For animated sprites
    float fps = 0.0f;     // 0 = no animation
    ParticleBlendMode blend = ParticleBlendMode::ALPHA;
};

/**
 * @brief Single emitter configuration
 */
struct EmitterConfig {
    std::string name = "Emitter";
    EmitterShape shape = EmitterShape::POINT;
    ParticleRenderShape renderShape = ParticleRenderShape::CIRCLE;

    float duration = 1.0f;
    bool loop = true;

    BurstConfig burst;
    RangeValue rate = {10.0f, 10.0f};  // Particles per second

    RangeValue angle = {0.0f, 360.0f};
    RangeValue speed = {1.0f, 5.0f};
    RangeValue lifetime = {0.5f, 2.0f};

    glm::vec2 gravity = {0.0f, -9.8f};
    float drag = 0.0f;  // Velocity damping

    // Size over lifetime
    glm::vec2 sizeStart = {1.0f, 1.0f};
    glm::vec2 sizeEnd = {0.0f, 0.0f};
    std::vector<CurveKey> sizeCurve;  // Empty = linear

    // Color over lifetime (simple start/end)
    glm::vec4 colorStart = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    glm::vec4 colorEnd = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);

    // Color gradient (advanced)
    std::vector<ColorStop> colorGradient;

    // Texture
    ParticleTextureConfig texture;

    // Emitter shape-specific settings
    float coneAngle = 30.0f;        // For CONE
    glm::vec3 boxSize = {1, 1, 1};  // For BOX
    float circleRadius = 1.0f;      // For CIRCLE

    bool alignToVelocity = false;
    uint32_t seed = 0;  // 0 = random each time
};

/**
 * @brief Complete particle effect with multiple emitters
 */
struct ParticleEffect {
    std::string name = "New Effect";
    int version = 1;
    std::vector<EmitterConfig> emitters;

    /**
     * @brief Create a simple default effect
     */
    static ParticleEffect createDefault();
};
