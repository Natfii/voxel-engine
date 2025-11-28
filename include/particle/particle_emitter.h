/**
 * @file particle_emitter.h
 * @brief Particle emitter that spawns and updates particles
 */

#pragma once

#include "particle/particle_effect.h"
#include <glm/glm.hpp>
#include <vector>
#include <random>

/**
 * @brief Runtime particle data
 */
struct RuntimeParticle {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 velocity = glm::vec3(0.0f);
    glm::vec4 color = glm::vec4(1.0f);
    glm::vec2 size = glm::vec2(1.0f);
    float lifetime = 0.0f;
    float maxLifetime = 1.0f;
    float rotation = 0.0f;
    int frameIndex = 0;

    bool isAlive() const { return lifetime > 0.0f; }
    float getNormalizedAge() const {
        return maxLifetime > 0.0f ? 1.0f - (lifetime / maxLifetime) : 1.0f;
    }
};

/**
 * @brief Spawns and updates particles based on EmitterConfig
 */
class ParticleEmitter {
public:
    ParticleEmitter();
    explicit ParticleEmitter(const EmitterConfig& config);

    /**
     * @brief Set emitter configuration
     */
    void setConfig(const EmitterConfig& config);

    /**
     * @brief Get current configuration
     */
    const EmitterConfig& getConfig() const { return m_config; }

    /**
     * @brief Update particles
     * @param deltaTime Frame delta time
     */
    void update(float deltaTime);

    /**
     * @brief Reset emitter to initial state
     */
    void reset();

    /**
     * @brief Get all particles
     */
    const std::vector<RuntimeParticle>& getParticles() const { return m_particles; }

    /**
     * @brief Get number of active particles
     */
    size_t getActiveCount() const;

    /**
     * @brief Set emitter position
     */
    void setPosition(const glm::vec3& pos) { m_position = pos; }

    /**
     * @brief Get emitter position
     */
    const glm::vec3& getPosition() const { return m_position; }

    /**
     * @brief Check if emitter is finished (non-looping and duration elapsed)
     */
    bool isFinished() const;

    /**
     * @brief Force spawn a burst of particles
     */
    void burst(int count);

private:
    void spawnParticle();
    void updateParticle(RuntimeParticle& p, float dt);
    glm::vec3 getSpawnPosition();
    glm::vec3 getSpawnVelocity();
    glm::vec4 evaluateColor(float normalizedAge);
    glm::vec2 evaluateSize(float normalizedAge);
    float evaluateCurve(const std::vector<CurveKey>& curve, float t, float defaultValue);

    EmitterConfig m_config;
    std::vector<RuntimeParticle> m_particles;
    glm::vec3 m_position = glm::vec3(0.0f);

    float m_time = 0.0f;
    float m_spawnAccumulator = 0.0f;
    int m_burstCyclesRemaining = 0;
    float m_burstTimer = 0.0f;

    std::mt19937 m_rng;
    std::uniform_real_distribution<float> m_dist01{0.0f, 1.0f};
};
