#include "particle_system.h"
#include <random>
#include <algorithm>
#include <cctype>
#include <glm/gtc/constants.hpp>

ParticleSystem::ParticleSystem() {
    m_particles.reserve(1000);  // Reserve space for efficiency
}

ParticleSystem::~ParticleSystem() {
}

void ParticleSystem::update(float deltaTime) {
    // Update all particles
    for (auto& particle : m_particles) {
        // Apply gravity
        particle.velocity.y += m_gravity * deltaTime;

        // Update position
        particle.position += particle.velocity * deltaTime;

        // Decrease lifetime
        particle.lifetime -= deltaTime;
    }

    // Remove dead particles
    m_particles.erase(
        std::remove_if(m_particles.begin(), m_particles.end(),
            [](const Particle& p) { return !p.isAlive(); }),
        m_particles.end()
    );
}

void ParticleSystem::spawnWaterSplash(const glm::vec3& position, float intensity) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<float> angleDist(0.0f, glm::two_pi<float>());
    std::uniform_real_distribution<float> speedDist(0.5f, 2.5f);
    std::uniform_real_distribution<float> upDist(2.0f, 4.0f);
    std::uniform_real_distribution<float> lifeDist(0.3f, 0.8f);
    std::uniform_real_distribution<float> sizeDist(0.05f, 0.15f);

    // Clamp intensity
    int particleCount = static_cast<int>(intensity * 5.0f);
    particleCount = std::clamp(particleCount, 5, 50);

    // Water color (light blue/white)
    glm::vec3 waterColor(0.7f, 0.85f, 1.0f);

    for (int i = 0; i < particleCount; i++) {
        float angle = angleDist(gen);
        float speed = speedDist(gen);
        float upSpeed = upDist(gen);

        glm::vec3 velocity(
            cos(angle) * speed,
            upSpeed,
            sin(angle) * speed
        );

        float lifetime = lifeDist(gen);
        float size = sizeDist(gen);

        spawnParticle(position, velocity, waterColor, lifetime, size);
    }
}

void ParticleSystem::spawnLavaSplash(const glm::vec3& position, float intensity) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<float> angleDist(0.0f, glm::two_pi<float>());
    std::uniform_real_distribution<float> speedDist(0.3f, 1.5f);
    std::uniform_real_distribution<float> upDist(1.0f, 3.0f);
    std::uniform_real_distribution<float> lifeDist(0.5f, 1.2f);
    std::uniform_real_distribution<float> sizeDist(0.08f, 0.2f);
    std::uniform_real_distribution<float> colorVar(0.9f, 1.0f);

    // Clamp intensity
    int particleCount = static_cast<int>(intensity * 4.0f);
    particleCount = std::clamp(particleCount, 3, 30);

    for (int i = 0; i < particleCount; i++) {
        float angle = angleDist(gen);
        float speed = speedDist(gen);
        float upSpeed = upDist(gen);

        glm::vec3 velocity(
            cos(angle) * speed,
            upSpeed,
            sin(angle) * speed
        );

        // Lava colors (red/orange/yellow variations)
        glm::vec3 lavaColor(
            1.0f,
            0.3f * colorVar(gen),
            0.1f * colorVar(gen)
        );

        float lifetime = lifeDist(gen);
        float size = sizeDist(gen);

        spawnParticle(position, velocity, lavaColor, lifetime, size);
    }
}

bool ParticleSystem::spawnParticleEffect(const std::string& effectName, const glm::vec3& position, float intensity) {
    // Normalize effect name to lowercase for case-insensitive comparison
    std::string normalizedName = effectName;
    std::transform(normalizedName.begin(), normalizedName.end(), normalizedName.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Map effect names to spawn methods
    if (normalizedName == "water_splash" || normalizedName == "water") {
        spawnWaterSplash(position, intensity);
        return true;
    }
    else if (normalizedName == "lava_splash" || normalizedName == "lava") {
        spawnLavaSplash(position, intensity);
        return true;
    }
    else if (normalizedName == "explosion") {
        // Spawn explosion effect (orange/red particles bursting outward)
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_real_distribution<float> angleDist(0.0f, glm::two_pi<float>());
        std::uniform_real_distribution<float> pitchDist(-glm::pi<float>() / 6.0f, glm::pi<float>() / 6.0f);
        std::uniform_real_distribution<float> speedDist(2.0f, 5.0f);
        std::uniform_real_distribution<float> lifeDist(0.4f, 0.9f);
        std::uniform_real_distribution<float> sizeDist(0.1f, 0.25f);
        std::uniform_real_distribution<float> colorVar(0.8f, 1.0f);

        int particleCount = static_cast<int>(intensity * 8.0f);
        particleCount = std::clamp(particleCount, 10, 80);

        for (int i = 0; i < particleCount; i++) {
            float angle = angleDist(gen);
            float pitch = pitchDist(gen);
            float speed = speedDist(gen);

            glm::vec3 velocity(
                cos(angle) * cos(pitch) * speed,
                sin(pitch) * speed,
                sin(angle) * cos(pitch) * speed
            );

            // Explosion colors (red/orange variations)
            glm::vec3 explosionColor(
                colorVar(gen),
                0.4f * colorVar(gen),
                0.0f
            );

            float lifetime = lifeDist(gen);
            float size = sizeDist(gen);

            spawnParticle(position, velocity, explosionColor, lifetime, size);
        }
        return true;
    }
    else if (normalizedName == "smoke") {
        // Spawn smoke effect (gray particles rising slowly)
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_real_distribution<float> angleDist(0.0f, glm::two_pi<float>());
        std::uniform_real_distribution<float> speedDist(0.1f, 0.5f);
        std::uniform_real_distribution<float> upDist(1.0f, 2.0f);
        std::uniform_real_distribution<float> lifeDist(1.0f, 2.5f);
        std::uniform_real_distribution<float> sizeDist(0.15f, 0.3f);
        std::uniform_real_distribution<float> colorVar(0.3f, 0.6f);

        int particleCount = static_cast<int>(intensity * 3.0f);
        particleCount = std::clamp(particleCount, 3, 25);

        for (int i = 0; i < particleCount; i++) {
            float angle = angleDist(gen);
            float speed = speedDist(gen);
            float upSpeed = upDist(gen);

            glm::vec3 velocity(
                cos(angle) * speed,
                upSpeed,
                sin(angle) * speed
            );

            // Smoke color (varying shades of gray)
            float gray = colorVar(gen);
            glm::vec3 smokeColor(gray, gray, gray);

            float lifetime = lifeDist(gen);
            float size = sizeDist(gen);

            spawnParticle(position, velocity, smokeColor, lifetime, size);
        }
        return true;
    }

    // Unknown effect name
    return false;
}

void ParticleSystem::spawnParticle(const glm::vec3& position, const glm::vec3& velocity,
                                  const glm::vec3& color, float lifetime, float size) {
    // Don't spawn if we have too many particles
    if (m_particles.size() >= 1000) {
        return;
    }

    m_particles.emplace_back(position, velocity, color, lifetime, size);
}

void ParticleSystem::clear() {
    m_particles.clear();
}
