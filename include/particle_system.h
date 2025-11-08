#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <memory>

class VulkanRenderer;

/**
 * @brief Simple particle for water splashes and effects
 */
struct Particle {
    glm::vec3 position;
    glm::vec3 velocity;
    glm::vec3 color;
    float lifetime;      // Time remaining (seconds)
    float maxLifetime;   // Total lifetime
    float size;          // Particle size

    Particle(const glm::vec3& pos, const glm::vec3& vel, const glm::vec3& col, float life, float sz)
        : position(pos), velocity(vel), color(col), lifetime(life), maxLifetime(life), size(sz) {}

    bool isAlive() const { return lifetime > 0.0f; }
    float getAlpha() const { return lifetime / maxLifetime; }
};

/**
 * @brief Manages particle effects for water splashes
 */
class ParticleSystem {
public:
    ParticleSystem();
    ~ParticleSystem();

    /**
     * @brief Updates all active particles
     * @param deltaTime Time elapsed since last frame
     */
    void update(float deltaTime);

    /**
     * @brief Spawns water splash particles at position
     * @param position World position for splash
     * @param intensity Number of particles (1-10 scale)
     */
    void spawnWaterSplash(const glm::vec3& position, float intensity = 1.0f);

    /**
     * @brief Spawns lava splash particles at position
     * @param position World position for splash
     * @param intensity Number of particles (1-10 scale)
     */
    void spawnLavaSplash(const glm::vec3& position, float intensity = 1.0f);

    /**
     * @brief Gets all active particles for rendering
     */
    const std::vector<Particle>& getParticles() const { return m_particles; }

    /**
     * @brief Clears all particles
     */
    void clear();

private:
    std::vector<Particle> m_particles;
    const float m_gravity = -9.8f;  // Gravity acceleration

    void spawnParticle(const glm::vec3& position, const glm::vec3& velocity,
                      const glm::vec3& color, float lifetime, float size);
};
