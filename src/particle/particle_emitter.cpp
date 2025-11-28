/**
 * @file particle_emitter.cpp
 * @brief Implementation of particle emitter
 */

#include "particle/particle_emitter.h"
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

ParticleEmitter::ParticleEmitter() {
    std::random_device rd;
    m_rng.seed(rd());
}

ParticleEmitter::ParticleEmitter(const EmitterConfig& config) : ParticleEmitter() {
    setConfig(config);
}

void ParticleEmitter::setConfig(const EmitterConfig& config) {
    m_config = config;

    // Seed RNG if specified
    if (config.seed != 0) {
        m_rng.seed(config.seed);
    }

    // Initialize burst cycles
    m_burstCyclesRemaining = config.burst.cycles;
    m_burstTimer = 0.0f;

    // Reserve particle space
    size_t maxParticles = static_cast<size_t>(
        std::max(config.rate.max, static_cast<float>(config.burst.count)) *
        config.lifetime.max * 2.0f);
    m_particles.reserve(std::min(maxParticles, size_t(10000)));
}

void ParticleEmitter::update(float deltaTime) {
    m_time += deltaTime;

    // Safety limits to prevent crashes from extreme values
    static constexpr size_t MAX_PARTICLES = 10000;
    static constexpr int MAX_SPAWNS_PER_FRAME = 100;

    // Check if emitter is finished
    if (!m_config.loop && m_time >= m_config.duration) {
        // Only update existing particles, don't spawn new ones
    } else {
        // Spawn particles based on rate (with safety limits)
        float rate = m_config.rate.random();
        m_spawnAccumulator += rate * deltaTime;

        int spawnsThisFrame = 0;
        while (m_spawnAccumulator >= 1.0f && spawnsThisFrame < MAX_SPAWNS_PER_FRAME) {
            if (m_particles.size() < MAX_PARTICLES) {
                spawnParticle();
            }
            m_spawnAccumulator -= 1.0f;
            spawnsThisFrame++;
        }
        // Cap accumulator to prevent runaway spawning
        if (m_spawnAccumulator > 10.0f) {
            m_spawnAccumulator = 10.0f;
        }

        // Handle burst spawning
        if (m_config.burst.count > 0 && m_burstCyclesRemaining > 0) {
            m_burstTimer += deltaTime;
            if (m_burstTimer >= m_config.burst.interval || m_time < deltaTime) {
                burst(std::min(m_config.burst.count, MAX_SPAWNS_PER_FRAME));
                m_burstTimer = 0.0f;
                m_burstCyclesRemaining--;
            }
        }
    }

    // Update all particles
    for (auto& particle : m_particles) {
        if (particle.isAlive()) {
            updateParticle(particle, deltaTime);
        }
    }

    // Remove dead particles (swap and pop for efficiency)
    m_particles.erase(
        std::remove_if(m_particles.begin(), m_particles.end(),
            [](const RuntimeParticle& p) { return !p.isAlive(); }),
        m_particles.end());
}

void ParticleEmitter::reset() {
    m_particles.clear();
    m_time = 0.0f;
    m_spawnAccumulator = 0.0f;
    m_burstCyclesRemaining = m_config.burst.cycles;
    m_burstTimer = 0.0f;
}

size_t ParticleEmitter::getActiveCount() const {
    return std::count_if(m_particles.begin(), m_particles.end(),
        [](const RuntimeParticle& p) { return p.isAlive(); });
}

bool ParticleEmitter::isFinished() const {
    if (m_config.loop) return false;
    return m_time >= m_config.duration && getActiveCount() == 0;
}

void ParticleEmitter::burst(int count) {
    for (int i = 0; i < count; ++i) {
        spawnParticle();
    }
}

void ParticleEmitter::spawnParticle() {
    RuntimeParticle p;
    p.position = getSpawnPosition();
    p.velocity = getSpawnVelocity();
    p.lifetime = m_config.lifetime.random();
    p.maxLifetime = p.lifetime;
    p.size = m_config.sizeStart;
    p.rotation = m_dist01(m_rng) * glm::two_pi<float>();
    p.frameIndex = m_config.texture.frameIndex;

    // Initial color - use colorStart (will be updated by evaluateColor each frame)
    if (!m_config.colorGradient.empty()) {
        p.color = m_config.colorGradient[0].color;
    } else {
        p.color = m_config.colorStart;
    }

    m_particles.push_back(p);
}

void ParticleEmitter::updateParticle(RuntimeParticle& p, float dt) {
    // Apply gravity
    p.velocity.x += m_config.gravity.x * dt;
    p.velocity.y += m_config.gravity.y * dt;

    // Apply drag
    if (m_config.drag > 0.0f) {
        p.velocity *= (1.0f - m_config.drag * dt);
    }

    // Update position
    p.position += p.velocity * dt;

    // Update lifetime
    p.lifetime -= dt;

    // Update properties over lifetime
    float age = p.getNormalizedAge();
    p.color = evaluateColor(age);
    p.size = evaluateSize(age);

    // Update rotation if aligned to velocity
    if (m_config.alignToVelocity && glm::length(p.velocity) > 0.001f) {
        p.rotation = std::atan2(p.velocity.y, p.velocity.x);
    }

    // Animate texture frames
    if (m_config.texture.fps > 0.0f && m_config.texture.frameCount > 1) {
        float frameTime = 1.0f / m_config.texture.fps;
        int frameOffset = static_cast<int>((p.maxLifetime - p.lifetime) / frameTime);
        p.frameIndex = m_config.texture.frameIndex +
            (frameOffset % m_config.texture.frameCount);
    }
}

glm::vec3 ParticleEmitter::getSpawnPosition() {
    glm::vec3 pos = m_position;

    switch (m_config.shape) {
        case EmitterShape::POINT:
            // Spawn at exact position
            break;

        case EmitterShape::BOX: {
            float x = (m_dist01(m_rng) - 0.5f) * m_config.boxSize.x;
            float y = (m_dist01(m_rng) - 0.5f) * m_config.boxSize.y;
            float z = (m_dist01(m_rng) - 0.5f) * m_config.boxSize.z;
            pos += glm::vec3(x, y, z);
            break;
        }

        case EmitterShape::CIRCLE: {
            float angle = m_dist01(m_rng) * glm::two_pi<float>();
            float radius = std::sqrt(m_dist01(m_rng)) * m_config.circleRadius;
            pos.x += std::cos(angle) * radius;
            pos.y += std::sin(angle) * radius;  // Use X/Y plane for 2D preview
            break;
        }

        case EmitterShape::CONE:
            // Cone spawns at point, direction varies
            break;
    }

    return pos;
}

glm::vec3 ParticleEmitter::getSpawnVelocity() {
    float speed = m_config.speed.random();

    // Get angle range
    float minAngle = glm::radians(m_config.angle.min);
    float maxAngle = glm::radians(m_config.angle.max);
    float angle = minAngle + m_dist01(m_rng) * (maxAngle - minAngle);

    glm::vec3 velocity;

    if (m_config.shape == EmitterShape::CONE) {
        // Cone emission - spread within cone angle (2D: spray upward with spread)
        float coneHalfAngle = glm::radians(m_config.coneAngle * 0.5f);
        float spreadAngle = (m_dist01(m_rng) * 2.0f - 1.0f) * coneHalfAngle;  // -cone to +cone
        float baseAngle = glm::radians(90.0f);  // Up direction

        velocity.x = std::cos(baseAngle + spreadAngle) * speed;
        velocity.y = std::sin(baseAngle + spreadAngle) * speed;
        velocity.z = 0.0f;
    } else {
        // 2D angle-based emission
        velocity.x = std::cos(angle) * speed;
        velocity.y = std::sin(angle) * speed;
        velocity.z = 0.0f;
    }

    return velocity;
}

glm::vec4 ParticleEmitter::evaluateColor(float normalizedAge) {
    const auto& gradient = m_config.colorGradient;

    glm::vec4 result;

    // Use simple start/end interpolation if no gradient defined
    if (gradient.empty()) {
        result = glm::mix(m_config.colorStart, m_config.colorEnd, normalizedAge);
    }
    else if (gradient.size() == 1) {
        result = gradient[0].color;
    }
    else {
        // Find surrounding stops
        size_t i = 0;
        while (i < gradient.size() - 1 && gradient[i + 1].time <= normalizedAge) {
            ++i;
        }

        if (i >= gradient.size() - 1) {
            result = gradient.back().color;
        } else {
            // Interpolate between stops
            const ColorStop& a = gradient[i];
            const ColorStop& b = gradient[i + 1];

            // Guard against division by zero if stops have same time
            float timeDiff = b.time - a.time;
            if (timeDiff < 0.0001f) {
                result = a.color;
            } else {
                float t = (normalizedAge - a.time) / timeDiff;
                t = glm::clamp(t, 0.0f, 1.0f);
                result = glm::mix(a.color, b.color, t);
            }
        }
    }

    // Clamp color to valid 0-1 range to prevent crashes from extreme values
    result.r = glm::clamp(result.r, 0.0f, 1.0f);
    result.g = glm::clamp(result.g, 0.0f, 1.0f);
    result.b = glm::clamp(result.b, 0.0f, 1.0f);
    result.a = glm::clamp(result.a, 0.0f, 1.0f);

    return result;
}

glm::vec2 ParticleEmitter::evaluateSize(float normalizedAge) {
    // Linear interpolation by default
    float t = normalizedAge;

    // Apply curve if present
    if (!m_config.sizeCurve.empty()) {
        t = evaluateCurve(m_config.sizeCurve, normalizedAge, normalizedAge);
    }

    return glm::mix(m_config.sizeStart, m_config.sizeEnd, t);
}

float ParticleEmitter::evaluateCurve(const std::vector<CurveKey>& curve, float t, float defaultValue) {
    if (curve.empty()) {
        return defaultValue;
    }

    if (curve.size() == 1) {
        return curve[0].value;
    }

    // Find surrounding keys
    size_t i = 0;
    while (i < curve.size() - 1 && curve[i + 1].time <= t) {
        ++i;
    }

    if (i >= curve.size() - 1) {
        return curve.back().value;
    }

    // Linear interpolation between keys
    const CurveKey& a = curve[i];
    const CurveKey& b = curve[i + 1];

    // Guard against division by zero if keys have same time
    float timeDiff = b.time - a.time;
    if (timeDiff < 0.0001f) {
        return a.value;
    }

    float localT = (t - a.time) / timeDiff;
    return glm::mix(a.value, b.value, localT);
}
