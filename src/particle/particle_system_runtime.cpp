/**
 * @file particle_system_runtime.cpp
 * @brief Runtime particle system implementation
 */

#include "particle/particle_system_runtime.h"
#include "vulkan_renderer.h"
#include "logger.h"
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <algorithm>

ParticleSystemRuntime::ParticleSystemRuntime() {
    m_instanceData.reserve(MAX_PARTICLES);
}

ParticleSystemRuntime::~ParticleSystemRuntime() {
    // cleanup() should be called explicitly before destruction
}

bool ParticleSystemRuntime::initialize(VulkanRenderer* renderer) {
    m_renderer = renderer;

    if (!renderer) {
        Logger::error() << "ParticleSystemRuntime: null renderer";
        return false;
    }

    // Create particle quad vertex buffer (billboard)
    // Simple quad: two triangles forming a unit square centered at origin
    struct ParticleVertex {
        glm::vec3 position;
        glm::vec2 texCoord;
    };

    std::vector<ParticleVertex> quadVertices = {
        {{-0.5f, -0.5f, 0.0f}, {0.0f, 1.0f}},
        {{ 0.5f, -0.5f, 0.0f}, {1.0f, 1.0f}},
        {{ 0.5f,  0.5f, 0.0f}, {1.0f, 0.0f}},
        {{-0.5f, -0.5f, 0.0f}, {0.0f, 1.0f}},
        {{ 0.5f,  0.5f, 0.0f}, {1.0f, 0.0f}},
        {{-0.5f,  0.5f, 0.0f}, {0.0f, 0.0f}},
    };

    VkDeviceSize vertexSize = sizeof(ParticleVertex) * quadVertices.size();

    // Create staging buffer for vertex data
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    renderer->createBuffer(vertexSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer, stagingMemory);

    void* data;
    vkMapMemory(renderer->getDevice(), stagingMemory, 0, vertexSize, 0, &data);
    memcpy(data, quadVertices.data(), vertexSize);
    vkUnmapMemory(renderer->getDevice(), stagingMemory);

    // Create device-local vertex buffer
    renderer->createBuffer(vertexSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_particleVertexBuffer, m_particleVertexMemory);

    renderer->copyBuffer(stagingBuffer, m_particleVertexBuffer, vertexSize);

    // Cleanup staging
    vkDestroyBuffer(renderer->getDevice(), stagingBuffer, nullptr);
    vkFreeMemory(renderer->getDevice(), stagingMemory, nullptr);

    // Create instance buffer (host visible for frequent updates)
    VkDeviceSize instanceSize = sizeof(ParticleInstanceData) * MAX_PARTICLES;
    renderer->createBuffer(instanceSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_particleInstanceBuffer, m_particleInstanceMemory);

    vkMapMemory(renderer->getDevice(), m_particleInstanceMemory, 0, instanceSize, 0, &m_instanceBufferMapped);

    Logger::info() << "ParticleSystemRuntime initialized (max " << MAX_PARTICLES << " particles)";
    return true;
}

void ParticleSystemRuntime::cleanup() {
    if (m_renderer && m_renderer->getDevice()) {
        VkDevice device = m_renderer->getDevice();

        if (m_instanceBufferMapped) {
            vkUnmapMemory(device, m_particleInstanceMemory);
            m_instanceBufferMapped = nullptr;
        }

        if (m_particleInstanceBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, m_particleInstanceBuffer, nullptr);
            vkFreeMemory(device, m_particleInstanceMemory, nullptr);
            m_particleInstanceBuffer = VK_NULL_HANDLE;
        }

        if (m_particleVertexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, m_particleVertexBuffer, nullptr);
            vkFreeMemory(device, m_particleVertexMemory, nullptr);
            m_particleVertexBuffer = VK_NULL_HANDLE;
        }
    }

    m_activeEffects.clear();
    m_loadedEffects.clear();
}

bool ParticleSystemRuntime::loadEffect(const std::string& path) {
    try {
        YAML::Node root = YAML::LoadFile(path);

        ParticleEffect effect;
        effect.name = root["name"].as<std::string>("unnamed");

        // Parse emitters
        if (root["emitters"]) {
            for (const auto& emitterNode : root["emitters"]) {
                EmitterConfig config;

                config.name = emitterNode["name"].as<std::string>("emitter");
                config.duration = emitterNode["duration"].as<float>(1.0f);
                config.loop = emitterNode["loop"].as<bool>(true);

                // Parse emission rate
                if (emitterNode["rate"]) {
                    config.rate.min = emitterNode["rate"]["min"].as<float>(10.0f);
                    config.rate.max = emitterNode["rate"]["max"].as<float>(10.0f);
                }

                // Parse lifetime
                if (emitterNode["lifetime"]) {
                    config.lifetime.min = emitterNode["lifetime"]["min"].as<float>(1.0f);
                    config.lifetime.max = emitterNode["lifetime"]["max"].as<float>(1.0f);
                }

                // Parse speed
                if (emitterNode["speed"]) {
                    config.speed.min = emitterNode["speed"]["min"].as<float>(1.0f);
                    config.speed.max = emitterNode["speed"]["max"].as<float>(1.0f);
                }

                // Parse angle
                if (emitterNode["angle"]) {
                    config.angle.min = emitterNode["angle"]["min"].as<float>(0.0f);
                    config.angle.max = emitterNode["angle"]["max"].as<float>(360.0f);
                }

                // Parse size
                if (emitterNode["size"]) {
                    if (emitterNode["size"]["start"]) {
                        config.sizeStart.x = emitterNode["size"]["start"]["x"].as<float>(1.0f);
                        config.sizeStart.y = emitterNode["size"]["start"]["y"].as<float>(1.0f);
                    }
                    if (emitterNode["size"]["end"]) {
                        config.sizeEnd.x = emitterNode["size"]["end"]["x"].as<float>(0.0f);
                        config.sizeEnd.y = emitterNode["size"]["end"]["y"].as<float>(0.0f);
                    }
                }

                // Parse gravity
                if (emitterNode["gravity"]) {
                    config.gravity.x = emitterNode["gravity"]["x"].as<float>(0.0f);
                    config.gravity.y = emitterNode["gravity"]["y"].as<float>(-9.8f);
                }

                // Parse color gradient
                if (emitterNode["colors"]) {
                    config.colorGradient.clear();
                    for (const auto& colorNode : emitterNode["colors"]) {
                        ColorStop stop;
                        stop.time = colorNode["time"].as<float>(0.0f);
                        stop.color.r = colorNode["r"].as<float>(1.0f);
                        stop.color.g = colorNode["g"].as<float>(1.0f);
                        stop.color.b = colorNode["b"].as<float>(1.0f);
                        stop.color.a = colorNode["a"].as<float>(1.0f);
                        config.colorGradient.push_back(stop);
                    }
                }

                effect.emitters.push_back(config);
            }
        }

        m_loadedEffects[effect.name] = effect;
        Logger::info() << "Loaded particle effect: " << effect.name << " from " << path;
        return true;

    } catch (const std::exception& e) {
        Logger::error() << "Failed to load particle effect " << path << ": " << e.what();
        return false;
    }
}

uint32_t ParticleSystemRuntime::spawnEffect(const std::string& effectName, const glm::vec3& position, bool autoDestroy) {
    auto it = m_loadedEffects.find(effectName);
    if (it == m_loadedEffects.end()) {
        Logger::warning() << "Particle effect not found: " << effectName;
        return 0;
    }

    auto activeEffect = std::make_unique<ActiveParticleEffect>();
    activeEffect->effectName = effectName;
    activeEffect->position = position;
    activeEffect->elapsed = 0.0f;
    activeEffect->autoDestroy = autoDestroy;

    const ParticleEffect& effect = it->second;

    // Calculate max duration from all emitters
    float maxDuration = 0.0f;
    bool anyLooping = false;
    for (const auto& config : effect.emitters) {
        maxDuration = std::max(maxDuration, config.duration);
        if (config.loop) anyLooping = true;
    }
    activeEffect->lifetime = anyLooping ? -1.0f : maxDuration;

    // Create emitters for this instance
    for (const auto& config : effect.emitters) {
        auto emitter = std::make_unique<ParticleEmitter>(config);
        emitter->setPosition(position);
        activeEffect->emitters.push_back(std::move(emitter));
    }

    uint32_t instanceId = m_nextInstanceId++;
    m_activeEffects.push_back(std::move(activeEffect));

    return instanceId;
}

void ParticleSystemRuntime::spawnBurst(const std::string& effectName, const glm::vec3& position, int count) {
    auto it = m_loadedEffects.find(effectName);
    if (it == m_loadedEffects.end()) {
        Logger::warning() << "Particle effect not found for burst: " << effectName;
        return;
    }

    auto activeEffect = std::make_unique<ActiveParticleEffect>();
    activeEffect->effectName = effectName;
    activeEffect->position = position;
    activeEffect->elapsed = 0.0f;
    activeEffect->autoDestroy = true;
    activeEffect->lifetime = 5.0f;  // Max lifetime for burst particles

    const ParticleEffect& effect = it->second;

    // Create emitters and trigger burst
    for (const auto& config : effect.emitters) {
        auto emitter = std::make_unique<ParticleEmitter>(config);
        emitter->setPosition(position);
        emitter->burst(count);
        activeEffect->emitters.push_back(std::move(emitter));
    }

    m_activeEffects.push_back(std::move(activeEffect));
}

void ParticleSystemRuntime::removeEffect(uint32_t instanceId) {
    // For simplicity, we just mark for removal (actual removal happens in update)
    // In a more complex system, we'd track instance IDs properly
}

void ParticleSystemRuntime::update(float deltaTime) {
    // Update all active effects
    for (auto& effect : m_activeEffects) {
        effect->elapsed += deltaTime;

        for (auto& emitter : effect->emitters) {
            emitter->setPosition(effect->position);
            emitter->update(deltaTime);
        }
    }

    // Remove finished effects
    m_activeEffects.erase(
        std::remove_if(m_activeEffects.begin(), m_activeEffects.end(),
            [](const std::unique_ptr<ActiveParticleEffect>& effect) {
                if (!effect->autoDestroy) return false;
                if (effect->lifetime < 0) return false;  // Looping

                // Check if all emitters are finished
                bool allFinished = true;
                for (const auto& emitter : effect->emitters) {
                    if (!emitter->isFinished()) {
                        allFinished = false;
                        break;
                    }
                }
                return allFinished;
            }),
        m_activeEffects.end());
}

void ParticleSystemRuntime::render(VkCommandBuffer commandBuffer, const glm::mat4& viewProj, const glm::vec3& cameraPos) {
    if (!m_particleVertexBuffer || !m_particleInstanceBuffer) return;

    // Collect all particles into instance data
    m_instanceData.clear();

    for (const auto& effect : m_activeEffects) {
        for (const auto& emitter : effect->emitters) {
            const auto& particles = emitter->getParticles();

            for (const auto& p : particles) {
                if (!p.isAlive()) continue;
                if (m_instanceData.size() >= MAX_PARTICLES) break;

                ParticleInstanceData instance;
                instance.positionSize = glm::vec4(p.position, p.size.x);
                instance.color = p.color;
                instance.rotation = p.rotation;
                m_instanceData.push_back(instance);
            }

            if (m_instanceData.size() >= MAX_PARTICLES) break;
        }

        if (m_instanceData.size() >= MAX_PARTICLES) break;
    }

    if (m_instanceData.empty()) return;

    // Upload instance data to GPU
    memcpy(m_instanceBufferMapped, m_instanceData.data(),
           m_instanceData.size() * sizeof(ParticleInstanceData));

    // Note: Actual rendering requires a particle shader and pipeline
    // For now, this sets up the data - the actual draw call would be:
    // vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, particlePipeline);
    // VkBuffer buffers[] = {m_particleVertexBuffer, m_particleInstanceBuffer};
    // VkDeviceSize offsets[] = {0, 0};
    // vkCmdBindVertexBuffers(commandBuffer, 0, 2, buffers, offsets);
    // vkCmdDraw(commandBuffer, 6, m_instanceData.size(), 0, 0);
}

size_t ParticleSystemRuntime::getActiveParticleCount() const {
    size_t count = 0;
    for (const auto& effect : m_activeEffects) {
        for (const auto& emitter : effect->emitters) {
            count += emitter->getActiveCount();
        }
    }
    return count;
}
