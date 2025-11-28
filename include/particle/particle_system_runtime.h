/**
 * @file particle_system_runtime.h
 * @brief Runtime particle system for rendering particles in game world
 */

#pragma once

#include <vulkan/vulkan.h>
#include "particle/particle_effect.h"
#include "particle/particle_emitter.h"
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>

class VulkanRenderer;

/**
 * @brief Active particle effect instance in the world
 */
struct ActiveParticleEffect {
    std::string effectName;
    glm::vec3 position;
    std::vector<std::unique_ptr<ParticleEmitter>> emitters;
    float lifetime;      // -1 for infinite (looping)
    float elapsed;
    bool autoDestroy;    // Remove when finished
};

/**
 * @brief Runtime particle system manager
 *
 * Manages particle effects in the game world, updating and rendering
 * all active particle instances each frame.
 */
class ParticleSystemRuntime {
public:
    ParticleSystemRuntime();
    ~ParticleSystemRuntime();

    /**
     * @brief Initialize the particle system
     * @param renderer Vulkan renderer for GPU resources
     * @return True on success
     */
    bool initialize(VulkanRenderer* renderer);

    /**
     * @brief Cleanup GPU resources
     */
    void cleanup();

    /**
     * @brief Load a particle effect from YAML file
     * @param path Path to effect YAML file
     * @return True on success
     */
    bool loadEffect(const std::string& path);

    /**
     * @brief Spawn a particle effect at a position
     * @param effectName Name of loaded effect
     * @param position World position
     * @param autoDestroy Remove when finished (default true)
     * @return Effect instance ID, or 0 on failure
     */
    uint32_t spawnEffect(const std::string& effectName, const glm::vec3& position, bool autoDestroy = true);

    /**
     * @brief Spawn a one-shot particle burst
     * @param effectName Name of loaded effect
     * @param position World position
     * @param count Number of particles to spawn
     */
    void spawnBurst(const std::string& effectName, const glm::vec3& position, int count);

    /**
     * @brief Remove an active effect
     * @param instanceId Effect instance ID
     */
    void removeEffect(uint32_t instanceId);

    /**
     * @brief Update all active particle effects
     * @param deltaTime Time since last frame
     */
    void update(float deltaTime);

    /**
     * @brief Render all particle effects
     * @param commandBuffer Vulkan command buffer
     * @param viewProj View-projection matrix
     * @param cameraPos Camera position for billboarding
     */
    void render(VkCommandBuffer commandBuffer, const glm::mat4& viewProj, const glm::vec3& cameraPos);

    /**
     * @brief Get total active particle count
     */
    size_t getActiveParticleCount() const;

    /**
     * @brief Get number of active effects
     */
    size_t getActiveEffectCount() const { return m_activeEffects.size(); }

private:
    VulkanRenderer* m_renderer = nullptr;

    // Loaded effect templates
    std::unordered_map<std::string, ParticleEffect> m_loadedEffects;

    // Active effect instances
    std::vector<std::unique_ptr<ActiveParticleEffect>> m_activeEffects;
    uint32_t m_nextInstanceId = 1;

    // GPU resources for rendering
    VkBuffer m_particleVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_particleVertexMemory = VK_NULL_HANDLE;
    VkBuffer m_particleInstanceBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_particleInstanceMemory = VK_NULL_HANDLE;
    void* m_instanceBufferMapped = nullptr;

    static constexpr size_t MAX_PARTICLES = 10000;

    // Particle instance data for GPU
    struct ParticleInstanceData {
        glm::vec4 positionSize;  // xyz = position, w = size
        glm::vec4 color;         // rgba
        float rotation;
        float padding[3];
    };

    std::vector<ParticleInstanceData> m_instanceData;
};
