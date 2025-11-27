#include "particle/particle_effect.h"
#include <random>
#include <mutex>

float RangeValue::random() const {
    // Thread-safe random number generation
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::mutex rngMutex;

    // Guard against min > max
    float safeMin = std::min(min, max);
    float safeMax = std::max(min, max);

    std::lock_guard<std::mutex> lock(rngMutex);
    std::uniform_real_distribution<float> dist(safeMin, safeMax);
    return dist(gen);
}

ParticleEffect ParticleEffect::createDefault() {
    ParticleEffect effect;
    effect.name = "Default Effect";
    effect.version = 1;

    EmitterConfig emitter;
    emitter.name = "Default Emitter";
    emitter.shape = EmitterShape::POINT;
    emitter.duration = 1.0f;
    emitter.loop = true;

    // Basic emission settings
    emitter.rate = RangeValue(10.0f);
    emitter.angle = RangeValue(0.0f, 360.0f);
    emitter.speed = RangeValue(1.0f, 3.0f);
    emitter.lifetime = RangeValue(1.0f, 2.0f);

    // Gravity
    emitter.gravity = glm::vec2(0.0f, -9.8f);
    emitter.drag = 0.0f;

    // Size over lifetime (start at 1, fade to 0)
    emitter.sizeStart = glm::vec2(1.0f, 1.0f);
    emitter.sizeEnd = glm::vec2(0.0f, 0.0f);

    // Color gradient (white to transparent)
    ColorStop start;
    start.time = 0.0f;
    start.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

    ColorStop end;
    end.time = 1.0f;
    end.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);

    emitter.colorGradient.push_back(start);
    emitter.colorGradient.push_back(end);

    // Basic texture config
    emitter.texture.blend = ParticleBlendMode::ALPHA;
    emitter.texture.frameCount = 1;
    emitter.texture.fps = 0.0f;

    effect.emitters.push_back(emitter);

    return effect;
}
