/**
 * @file engine_api.cpp
 * @brief Implementation of the main engine API
 */

#include "engine_api.h"
#include "world.h"
#include "vulkan_renderer.h"
#include "player.h"
#include "block_system.h"
#include "structure_system.h"
#include "biome_system.h"
#include "biome_map.h"
#include "raycast.h"
#include "mesh/mesh_renderer.h"
#include "mesh/mesh.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_set>

// ============================================================================
// Singleton Instance
// ============================================================================

EngineAPI& EngineAPI::instance() {
    static EngineAPI instance;
    return instance;
}

// ============================================================================
// Initialization
// ============================================================================

void EngineAPI::initialize(World* world, VulkanRenderer* renderer, Player* player) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_world = world;
    m_renderer = renderer;
    m_player = player;

    // Create mesh renderer if we have a renderer
    if (renderer && !m_meshRenderer) {
        m_meshRenderer = new MeshRenderer(renderer);
    }
}

bool EngineAPI::isInitialized() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_world != nullptr && m_renderer != nullptr && m_player != nullptr;
}

// ============================================================================
// Block Manipulation
// ============================================================================

bool EngineAPI::placeBlock(int x, int y, int z, int blockID) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_world || !m_renderer) return false;

    // Check if position is valid
    auto chunk = m_world->getChunkAtWorldPos(static_cast<float>(x),
                                             static_cast<float>(y),
                                             static_cast<float>(z));
    if (!chunk) return false;

    // Place the block
    m_world->placeBlock(static_cast<float>(x),
                        static_cast<float>(y),
                        static_cast<float>(z),
                        blockID,
                        m_renderer);
    return true;
}

bool EngineAPI::placeBlock(const glm::ivec3& pos, int blockID) {
    return placeBlock(pos.x, pos.y, pos.z, blockID);
}

bool EngineAPI::placeBlock(const glm::ivec3& pos, const std::string& blockName) {
    int blockID = BlockRegistry::instance().getID(blockName);
    if (blockID < 0) return false;
    return placeBlock(pos, blockID);
}

bool EngineAPI::breakBlock(int x, int y, int z) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_world || !m_renderer) return false;

    // Check if position is valid
    auto chunk = m_world->getChunkAtWorldPos(static_cast<float>(x),
                                             static_cast<float>(y),
                                             static_cast<float>(z));
    if (!chunk) return false;

    // Break the block
    m_world->breakBlock(static_cast<float>(x),
                        static_cast<float>(y),
                        static_cast<float>(z),
                        m_renderer);
    return true;
}

bool EngineAPI::breakBlock(const glm::ivec3& pos) {
    return breakBlock(pos.x, pos.y, pos.z);
}

bool EngineAPI::setBlockMetadata(const glm::ivec3& pos, uint8_t metadata) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_world) return false;

    auto chunk = m_world->getChunkAtWorldPos(static_cast<float>(pos.x),
                                             static_cast<float>(pos.y),
                                             static_cast<float>(pos.z));
    if (!chunk) return false;

    m_world->setBlockMetadataAt(static_cast<float>(pos.x),
                                static_cast<float>(pos.y),
                                static_cast<float>(pos.z),
                                metadata);
    return true;
}

uint8_t EngineAPI::getBlockMetadata(const glm::ivec3& pos) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_world) return 0;

    return m_world->getBlockMetadataAt(static_cast<float>(pos.x),
                                       static_cast<float>(pos.y),
                                       static_cast<float>(pos.z));
}

BlockQueryResult EngineAPI::getBlockAt(int x, int y, int z) {
    std::lock_guard<std::mutex> lock(m_mutex);
    BlockQueryResult result;
    result.valid = false;
    result.position = glm::ivec3(x, y, z);

    if (!m_world) return result;

    auto chunk = m_world->getChunkAtWorldPos(static_cast<float>(x),
                                             static_cast<float>(y),
                                             static_cast<float>(z));
    if (!chunk) return result;

    result.blockID = m_world->getBlockAt(static_cast<float>(x),
                                         static_cast<float>(y),
                                         static_cast<float>(z));
    result.blockName = BlockRegistry::instance().getBlockName(result.blockID);
    result.valid = true;

    return result;
}

BlockQueryResult EngineAPI::getBlockAt(const glm::ivec3& pos) {
    return getBlockAt(pos.x, pos.y, pos.z);
}

// ============================================================================
// Area Operations
// ============================================================================

int EngineAPI::fillArea(const glm::ivec3& start, const glm::ivec3& end, int blockID) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_world || !m_renderer) return 0;

    int count = 0;
    glm::ivec3 min = glm::min(start, end);
    glm::ivec3 max = glm::max(start, end);

    // Place blocks without regenerating mesh each time
    for (int y = min.y; y <= max.y; ++y) {
        for (int z = min.z; z <= max.z; ++z) {
            for (int x = min.x; x <= max.x; ++x) {
                m_world->setBlockAt(static_cast<float>(x),
                                   static_cast<float>(y),
                                   static_cast<float>(z),
                                   blockID,
                                   false);  // Don't regenerate yet
                count++;
            }
        }
    }

    // Regenerate affected chunks once
    regenerateAffectedChunks(min, max);

    return count;
}

int EngineAPI::fillArea(const glm::ivec3& start, const glm::ivec3& end, const std::string& blockName) {
    int blockID = BlockRegistry::instance().getID(blockName);
    if (blockID < 0) return 0;
    return fillArea(start, end, blockID);
}

int EngineAPI::replaceBlocks(const glm::ivec3& start, const glm::ivec3& end,
                            int fromBlockID, int toBlockID) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_world || !m_renderer) return 0;

    int count = 0;
    glm::ivec3 min = glm::min(start, end);
    glm::ivec3 max = glm::max(start, end);

    // Replace blocks without regenerating mesh each time
    for (int y = min.y; y <= max.y; ++y) {
        for (int z = min.z; z <= max.z; ++z) {
            for (int x = min.x; x <= max.x; ++x) {
                int current = m_world->getBlockAt(static_cast<float>(x),
                                                  static_cast<float>(y),
                                                  static_cast<float>(z));
                if (current == fromBlockID) {
                    m_world->setBlockAt(static_cast<float>(x),
                                       static_cast<float>(y),
                                       static_cast<float>(z),
                                       toBlockID,
                                       false);  // Don't regenerate yet
                    count++;
                }
            }
        }
    }

    // Regenerate affected chunks once
    if (count > 0) {
        regenerateAffectedChunks(min, max);
    }

    return count;
}

int EngineAPI::replaceBlocks(const glm::ivec3& start, const glm::ivec3& end,
                            const std::string& fromName, const std::string& toName) {
    int fromID = BlockRegistry::instance().getID(fromName);
    int toID = BlockRegistry::instance().getID(toName);
    if (fromID < 0 || toID < 0) return 0;
    return replaceBlocks(start, end, fromID, toID);
}

// ============================================================================
// Sphere Operations
// ============================================================================

int EngineAPI::fillSphere(const glm::vec3& center, float radius, int blockID) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_world || !m_renderer) return 0;

    int count = 0;
    float radiusSq = radius * radius;

    // Calculate bounding box
    glm::ivec3 min(std::floor(center.x - radius),
                   std::floor(center.y - radius),
                   std::floor(center.z - radius));
    glm::ivec3 max(std::ceil(center.x + radius),
                   std::ceil(center.y + radius),
                   std::ceil(center.z + radius));

    // Fill sphere
    for (int y = min.y; y <= max.y; ++y) {
        for (int z = min.z; z <= max.z; ++z) {
            for (int x = min.x; x <= max.x; ++x) {
                glm::vec3 pos(x, y, z);
                float distSq = glm::dot(pos - center, pos - center);

                if (distSq <= radiusSq) {
                    m_world->setBlockAt(static_cast<float>(x),
                                       static_cast<float>(y),
                                       static_cast<float>(z),
                                       blockID,
                                       false);
                    count++;
                }
            }
        }
    }

    // Regenerate affected chunks
    regenerateAffectedChunks(min, max);

    return count;
}

int EngineAPI::hollowSphere(const glm::vec3& center, float radius, int blockID, float thickness) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_world || !m_renderer) return 0;

    int count = 0;
    float outerRadiusSq = radius * radius;
    float innerRadius = std::max(0.0f, radius - thickness);
    float innerRadiusSq = innerRadius * innerRadius;

    // Calculate bounding box
    glm::ivec3 min(std::floor(center.x - radius),
                   std::floor(center.y - radius),
                   std::floor(center.z - radius));
    glm::ivec3 max(std::ceil(center.x + radius),
                   std::ceil(center.y + radius),
                   std::ceil(center.z + radius));

    // Fill hollow sphere
    for (int y = min.y; y <= max.y; ++y) {
        for (int z = min.z; z <= max.z; ++z) {
            for (int x = min.x; x <= max.x; ++x) {
                glm::vec3 pos(x, y, z);
                float distSq = glm::dot(pos - center, pos - center);

                // Check if in shell (between inner and outer radius)
                if (distSq <= outerRadiusSq && distSq >= innerRadiusSq) {
                    m_world->setBlockAt(static_cast<float>(x),
                                       static_cast<float>(y),
                                       static_cast<float>(z),
                                       blockID,
                                       false);
                    count++;
                }
            }
        }
    }

    // Regenerate affected chunks
    regenerateAffectedChunks(min, max);

    return count;
}

// ============================================================================
// Terrain Modification
// ============================================================================

float EngineAPI::calculateBrushInfluence(float distance, const BrushSettings& brush) {
    if (distance >= brush.radius) return 0.0f;

    float normalized = distance / brush.radius;

    // Apply falloff
    float influence = 1.0f - normalized;
    if (brush.falloff > 0.0f) {
        // Smooth falloff using smoothstep-like function
        influence = influence * influence * (3.0f - 2.0f * influence);
    }

    return influence * brush.strength;
}

int EngineAPI::raiseTerrain(const glm::vec3& center, float radius, float height,
                           const BrushSettings& brush) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_world || !m_renderer) return 0;

    int count = 0;
    float effectiveRadius = brush.radius > 0 ? brush.radius : radius;

    // Calculate bounding box
    glm::ivec3 min(std::floor(center.x - effectiveRadius),
                   std::floor(center.y),
                   std::floor(center.z - effectiveRadius));
    glm::ivec3 max(std::ceil(center.x + effectiveRadius),
                   std::ceil(center.y + height * 2),
                   std::ceil(center.z + effectiveRadius));

    // Raise terrain in a circular pattern
    for (int z = min.z; z <= max.z; ++z) {
        for (int x = min.x; x <= max.x; ++x) {
            float dx = x - center.x;
            float dz = z - center.z;
            float distance = std::sqrt(dx * dx + dz * dz);

            if (distance <= effectiveRadius) {
                float influence = calculateBrushInfluence(distance, brush);
                int raiseAmount = static_cast<int>(height * influence);

                // Find current surface
                int surfaceY = static_cast<int>(center.y);
                for (int y = static_cast<int>(center.y); y >= min.y; --y) {
                    int block = m_world->getBlockAt(static_cast<float>(x),
                                                    static_cast<float>(y),
                                                    static_cast<float>(z));
                    if (block != 0) {  // Found surface
                        surfaceY = y;
                        break;
                    }
                }

                // Get surface block type
                int surfaceBlock = m_world->getBlockAt(static_cast<float>(x),
                                                       static_cast<float>(surfaceY),
                                                       static_cast<float>(z));
                if (surfaceBlock == 0) surfaceBlock = 3;  // Default to grass

                // Place blocks upward
                for (int i = 1; i <= raiseAmount; ++i) {
                    m_world->setBlockAt(static_cast<float>(x),
                                       static_cast<float>(surfaceY + i),
                                       static_cast<float>(z),
                                       surfaceBlock,
                                       false);
                    count++;
                }
            }
        }
    }

    // Regenerate affected chunks
    if (count > 0) {
        regenerateAffectedChunks(min, max);
    }

    return count;
}

int EngineAPI::lowerTerrain(const glm::vec3& center, float radius, float depth,
                           const BrushSettings& brush) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_world || !m_renderer) return 0;

    int count = 0;
    float effectiveRadius = brush.radius > 0 ? brush.radius : radius;

    // Calculate bounding box
    glm::ivec3 min(std::floor(center.x - effectiveRadius),
                   std::floor(center.y - depth * 2),
                   std::floor(center.z - effectiveRadius));
    glm::ivec3 max(std::ceil(center.x + effectiveRadius),
                   std::ceil(center.y),
                   std::ceil(center.z + effectiveRadius));

    // Lower terrain in a circular pattern
    for (int z = min.z; z <= max.z; ++z) {
        for (int x = min.x; x <= max.x; ++x) {
            float dx = x - center.x;
            float dz = z - center.z;
            float distance = std::sqrt(dx * dx + dz * dz);

            if (distance <= effectiveRadius) {
                float influence = calculateBrushInfluence(distance, brush);
                int lowerAmount = static_cast<int>(depth * influence);

                // Find current surface
                int surfaceY = static_cast<int>(center.y);
                for (int y = static_cast<int>(center.y); y >= min.y; --y) {
                    int block = m_world->getBlockAt(static_cast<float>(x),
                                                    static_cast<float>(y),
                                                    static_cast<float>(z));
                    if (block != 0) {
                        surfaceY = y;
                        break;
                    }
                }

                // Remove blocks downward
                for (int i = 0; i < lowerAmount; ++i) {
                    m_world->setBlockAt(static_cast<float>(x),
                                       static_cast<float>(surfaceY - i),
                                       static_cast<float>(z),
                                       0,  // Air
                                       false);
                    count++;
                }
            }
        }
    }

    // Regenerate affected chunks
    if (count > 0) {
        regenerateAffectedChunks(min, max);
    }

    return count;
}

int EngineAPI::smoothTerrain(const glm::vec3& center, float radius,
                            const BrushSettings& brush) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_world || !m_renderer) return 0;

    int count = 0;
    float effectiveRadius = brush.radius > 0 ? brush.radius : radius;

    // Calculate bounding box
    glm::ivec3 min(std::floor(center.x - effectiveRadius),
                   std::floor(center.y - effectiveRadius),
                   std::floor(center.z - effectiveRadius));
    glm::ivec3 max(std::ceil(center.x + effectiveRadius),
                   std::ceil(center.y + effectiveRadius),
                   std::ceil(center.z + effectiveRadius));

    // First pass: calculate average height
    float avgHeight = 0.0f;
    int sampleCount = 0;

    for (int z = min.z; z <= max.z; ++z) {
        for (int x = min.x; x <= max.x; ++x) {
            float dx = x - center.x;
            float dz = z - center.z;
            float distance = std::sqrt(dx * dx + dz * dz);

            if (distance <= effectiveRadius) {
                // Find surface height
                for (int y = max.y; y >= min.y; --y) {
                    int block = m_world->getBlockAt(static_cast<float>(x),
                                                    static_cast<float>(y),
                                                    static_cast<float>(z));
                    if (block != 0) {
                        avgHeight += y;
                        sampleCount++;
                        break;
                    }
                }
            }
        }
    }

    if (sampleCount == 0) return 0;
    avgHeight /= sampleCount;

    // Second pass: smooth toward average
    for (int z = min.z; z <= max.z; ++z) {
        for (int x = min.x; x <= max.x; ++x) {
            float dx = x - center.x;
            float dz = z - center.z;
            float distance = std::sqrt(dx * dx + dz * dz);

            if (distance <= effectiveRadius) {
                float influence = calculateBrushInfluence(distance, brush);

                // Find current surface
                int surfaceY = static_cast<int>(center.y);
                int surfaceBlock = 3;  // Default to grass
                for (int y = max.y; y >= min.y; --y) {
                    int block = m_world->getBlockAt(static_cast<float>(x),
                                                    static_cast<float>(y),
                                                    static_cast<float>(z));
                    if (block != 0) {
                        surfaceY = y;
                        surfaceBlock = block;
                        break;
                    }
                }

                // Calculate target height
                int targetY = static_cast<int>(surfaceY * (1.0f - influence) + avgHeight * influence);

                // Adjust terrain
                if (targetY > surfaceY) {
                    // Raise
                    for (int y = surfaceY + 1; y <= targetY; ++y) {
                        m_world->setBlockAt(static_cast<float>(x),
                                           static_cast<float>(y),
                                           static_cast<float>(z),
                                           surfaceBlock,
                                           false);
                        count++;
                    }
                } else if (targetY < surfaceY) {
                    // Lower
                    for (int y = targetY + 1; y <= surfaceY; ++y) {
                        m_world->setBlockAt(static_cast<float>(x),
                                           static_cast<float>(y),
                                           static_cast<float>(z),
                                           0,
                                           false);
                        count++;
                    }
                }
            }
        }
    }

    // Regenerate affected chunks
    if (count > 0) {
        regenerateAffectedChunks(min, max);
    }

    return count;
}

int EngineAPI::paintTerrain(const glm::vec3& center, float radius, int blockID,
                           const BrushSettings& brush) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_world || !m_renderer) return 0;

    int count = 0;
    float effectiveRadius = brush.radius > 0 ? brush.radius : radius;

    // Calculate bounding box
    glm::ivec3 min(std::floor(center.x - effectiveRadius),
                   std::floor(center.y - effectiveRadius),
                   std::floor(center.z - effectiveRadius));
    glm::ivec3 max(std::ceil(center.x + effectiveRadius),
                   std::ceil(center.y + effectiveRadius),
                   std::ceil(center.z + effectiveRadius));

    // Paint surface blocks
    for (int z = min.z; z <= max.z; ++z) {
        for (int x = min.x; x <= max.x; ++x) {
            float dx = x - center.x;
            float dz = z - center.z;
            float distance = std::sqrt(dx * dx + dz * dz);

            if (distance <= effectiveRadius) {
                // Find surface
                for (int y = max.y; y >= min.y; --y) {
                    int block = m_world->getBlockAt(static_cast<float>(x),
                                                    static_cast<float>(y),
                                                    static_cast<float>(z));
                    if (block != 0) {
                        // Paint this block
                        m_world->setBlockAt(static_cast<float>(x),
                                           static_cast<float>(y),
                                           static_cast<float>(z),
                                           blockID,
                                           false);
                        count++;
                        break;
                    }
                }
            }
        }
    }

    // Regenerate affected chunks
    if (count > 0) {
        regenerateAffectedChunks(min, max);
    }

    return count;
}

int EngineAPI::flattenTerrain(const glm::vec3& center, float radius, int targetY,
                             const BrushSettings& brush) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_world || !m_renderer) return 0;

    int count = 0;
    float effectiveRadius = brush.radius > 0 ? brush.radius : radius;

    // Calculate bounding box
    glm::ivec3 min(std::floor(center.x - effectiveRadius),
                   std::min(targetY - 10, static_cast<int>(std::floor(center.y - effectiveRadius))),
                   std::floor(center.z - effectiveRadius));
    glm::ivec3 max(std::ceil(center.x + effectiveRadius),
                   std::max(targetY + 10, static_cast<int>(std::ceil(center.y + effectiveRadius))),
                   std::ceil(center.z + effectiveRadius));

    // Flatten terrain
    for (int z = min.z; z <= max.z; ++z) {
        for (int x = min.x; x <= max.x; ++x) {
            float dx = x - center.x;
            float dz = z - center.z;
            float distance = std::sqrt(dx * dx + dz * dz);

            if (distance <= effectiveRadius) {
                float influence = calculateBrushInfluence(distance, brush);

                // Find current surface
                int surfaceY = targetY;
                int surfaceBlock = 3;  // Default to grass
                for (int y = max.y; y >= min.y; --y) {
                    int block = m_world->getBlockAt(static_cast<float>(x),
                                                    static_cast<float>(y),
                                                    static_cast<float>(z));
                    if (block != 0) {
                        surfaceY = y;
                        surfaceBlock = block;
                        break;
                    }
                }

                // Calculate effective target
                int effectiveTarget = static_cast<int>(surfaceY * (1.0f - influence) + targetY * influence);

                // Adjust terrain to target height
                if (effectiveTarget > surfaceY) {
                    // Raise to target
                    for (int y = surfaceY + 1; y <= effectiveTarget; ++y) {
                        m_world->setBlockAt(static_cast<float>(x),
                                           static_cast<float>(y),
                                           static_cast<float>(z),
                                           surfaceBlock,
                                           false);
                        count++;
                    }
                } else if (effectiveTarget < surfaceY) {
                    // Lower to target
                    for (int y = effectiveTarget + 1; y <= surfaceY; ++y) {
                        m_world->setBlockAt(static_cast<float>(x),
                                           static_cast<float>(y),
                                           static_cast<float>(z),
                                           0,
                                           false);
                        count++;
                    }
                }
            }
        }
    }

    // Regenerate affected chunks
    if (count > 0) {
        regenerateAffectedChunks(min, max);
    }

    return count;
}

// ============================================================================
// Structure Spawning
// ============================================================================

bool EngineAPI::spawnStructure(const std::string& name, const glm::ivec3& position) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_world || !m_renderer) return false;

    return StructureRegistry::instance().spawnStructure(name, m_world, position, m_renderer);
}

bool EngineAPI::spawnStructure(const std::string& name, const glm::ivec3& position, int rotation) {
    // TODO: Implement rotation support in StructureRegistry
    // For now, just spawn without rotation
    return spawnStructure(name, position);
}

// ============================================================================
// Entity/Mesh Spawning
// ============================================================================

SpawnedEntity EngineAPI::spawnSphere(const glm::vec3& position, float radius,
                                     const glm::vec4& color) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SpawnedEntity entity;
    entity.entityID = m_nextEntityID++;
    entity.position = position;
    entity.type = "sphere";

    if (m_meshRenderer) {
        // Create sphere mesh
        Mesh sphereMesh = Mesh::createSphere(radius, 16, 16);
        uint32_t meshId = m_meshRenderer->createMesh(sphereMesh);

        // Create transform
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position);

        // Create instance
        m_meshRenderer->createInstance(meshId, transform, color);
    }

    m_spawnedEntities.push_back(entity);
    return entity;
}

SpawnedEntity EngineAPI::spawnCube(const glm::vec3& position, float size,
                                   const glm::vec4& color) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SpawnedEntity entity;
    entity.entityID = m_nextEntityID++;
    entity.position = position;
    entity.type = "cube";

    if (m_meshRenderer) {
        // Create cube mesh
        Mesh cubeMesh = Mesh::createCube(size);
        uint32_t meshId = m_meshRenderer->createMesh(cubeMesh);

        // Create transform
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position);

        // Create instance
        m_meshRenderer->createInstance(meshId, transform, color);
    }

    m_spawnedEntities.push_back(entity);
    return entity;
}

SpawnedEntity EngineAPI::spawnCylinder(const glm::vec3& position, float radius, float height,
                                       const glm::vec4& color) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SpawnedEntity entity;
    entity.entityID = m_nextEntityID++;
    entity.position = position;
    entity.type = "cylinder";

    if (m_meshRenderer) {
        // Create cylinder mesh
        Mesh cylinderMesh = Mesh::createCylinder(radius, height, 16);
        uint32_t meshId = m_meshRenderer->createMesh(cylinderMesh);

        // Create transform
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position);

        // Create instance
        m_meshRenderer->createInstance(meshId, transform, color);
    }

    m_spawnedEntities.push_back(entity);
    return entity;
}

SpawnedEntity EngineAPI::spawnMesh(const std::string& meshName, const glm::vec3& position,
                                   const glm::vec3& scale, const glm::vec3& rotation) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SpawnedEntity entity;
    entity.entityID = m_nextEntityID++;
    entity.position = position;
    entity.type = "mesh";

    if (m_meshRenderer) {
        // Load mesh from file
        std::string meshPath = "assets/meshes/" + meshName + ".obj";
        uint32_t meshId = m_meshRenderer->loadMeshFromFile(meshPath);

        // Create transform with rotation and scale
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position);
        transform = glm::rotate(transform, glm::radians(rotation.x), glm::vec3(1, 0, 0));
        transform = glm::rotate(transform, glm::radians(rotation.y), glm::vec3(0, 1, 0));
        transform = glm::rotate(transform, glm::radians(rotation.z), glm::vec3(0, 0, 1));
        transform = glm::scale(transform, scale);

        // Create instance
        m_meshRenderer->createInstance(meshId, transform);
    }

    m_spawnedEntities.push_back(entity);
    return entity;
}

bool EngineAPI::removeEntity(uint32_t entityID) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Find and remove entity
    auto it = std::find_if(m_spawnedEntities.begin(), m_spawnedEntities.end(),
                          [entityID](const SpawnedEntity& e) { return e.entityID == entityID; });

    if (it != m_spawnedEntities.end()) {
        // TODO: Remove from mesh renderer
        m_spawnedEntities.erase(it);
        return true;
    }

    return false;
}

bool EngineAPI::setEntityPosition(uint32_t entityID, const glm::vec3& position) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Find entity
    auto it = std::find_if(m_spawnedEntities.begin(), m_spawnedEntities.end(),
                          [entityID](const SpawnedEntity& e) { return e.entityID == entityID; });

    if (it != m_spawnedEntities.end()) {
        it->position = position;
        // TODO: Update mesh renderer instance transform
        return true;
    }

    return false;
}

bool EngineAPI::setEntityScale(uint32_t entityID, const glm::vec3& scale) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // TODO: Implement scale tracking and update mesh renderer
    return false;
}

bool EngineAPI::setEntityColor(uint32_t entityID, const glm::vec4& color) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // TODO: Update mesh renderer instance color
    return false;
}

std::vector<SpawnedEntity> EngineAPI::getAllEntities() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_spawnedEntities;
}

// ============================================================================
// World Queries
// ============================================================================

RaycastResult EngineAPI::raycast(const glm::vec3& origin, const glm::vec3& direction,
                                 float maxDistance) {
    std::lock_guard<std::mutex> lock(m_mutex);
    RaycastResult result;
    result.hit = false;

    if (!m_world) return result;

    // Use existing raycast system
    auto hit = Raycast::castRay(m_world, origin, direction, maxDistance);

    result.hit = hit.hit;
    result.position = hit.position;
    result.normal = hit.normal;
    result.blockPos = glm::ivec3(hit.blockX, hit.blockY, hit.blockZ);
    result.blockID = m_world->getBlockAt(static_cast<float>(hit.blockX),
                                         static_cast<float>(hit.blockY),
                                         static_cast<float>(hit.blockZ));
    result.distance = hit.distance;

    return result;
}

std::vector<BlockQueryResult> EngineAPI::getBlocksInRadius(const glm::vec3& center, float radius) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<BlockQueryResult> results;

    if (!m_world) return results;

    float radiusSq = radius * radius;
    glm::ivec3 min(std::floor(center.x - radius),
                   std::floor(center.y - radius),
                   std::floor(center.z - radius));
    glm::ivec3 max(std::ceil(center.x + radius),
                   std::ceil(center.y + radius),
                   std::ceil(center.z + radius));

    for (int y = min.y; y <= max.y; ++y) {
        for (int z = min.z; z <= max.z; ++z) {
            for (int x = min.x; x <= max.x; ++x) {
                glm::vec3 pos(x, y, z);
                float distSq = glm::dot(pos - center, pos - center);

                if (distSq <= radiusSq) {
                    auto chunk = m_world->getChunkAtWorldPos(static_cast<float>(x),
                                                             static_cast<float>(y),
                                                             static_cast<float>(z));
                    if (chunk) {
                        BlockQueryResult result;
                        result.valid = true;
                        result.position = glm::ivec3(x, y, z);
                        result.blockID = m_world->getBlockAt(static_cast<float>(x),
                                                             static_cast<float>(y),
                                                             static_cast<float>(z));
                        result.blockName = BlockRegistry::instance().getBlockName(result.blockID);
                        results.push_back(result);
                    }
                }
            }
        }
    }

    return results;
}

std::vector<BlockQueryResult> EngineAPI::getBlocksInArea(const glm::ivec3& start,
                                                        const glm::ivec3& end) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<BlockQueryResult> results;

    if (!m_world) return results;

    glm::ivec3 min = glm::min(start, end);
    glm::ivec3 max = glm::max(start, end);

    for (int y = min.y; y <= max.y; ++y) {
        for (int z = min.z; z <= max.z; ++z) {
            for (int x = min.x; x <= max.x; ++x) {
                auto chunk = m_world->getChunkAtWorldPos(static_cast<float>(x),
                                                         static_cast<float>(y),
                                                         static_cast<float>(z));
                if (chunk) {
                    BlockQueryResult result;
                    result.valid = true;
                    result.position = glm::ivec3(x, y, z);
                    result.blockID = m_world->getBlockAt(static_cast<float>(x),
                                                         static_cast<float>(y),
                                                         static_cast<float>(z));
                    result.blockName = BlockRegistry::instance().getBlockName(result.blockID);
                    results.push_back(result);
                }
            }
        }
    }

    return results;
}

std::string EngineAPI::getBiomeAt(float x, float z) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_world) return "unknown";

    auto biomeMap = m_world->getBiomeMap();
    if (!biomeMap) return "unknown";

    auto biome = biomeMap->getBiomeAt(x, z);
    if (!biome) return "unknown";

    return biome->name;
}

int EngineAPI::getHeightAt(float x, float z) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_world) return 0;

    auto biomeMap = m_world->getBiomeMap();
    if (!biomeMap) return 0;

    return biomeMap->getTerrainHeightAt(x, z);
}

// ============================================================================
// Player
// ============================================================================

glm::vec3 EngineAPI::getPlayerPosition() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_player) return glm::vec3(0.0f);
    return m_player->Position;
}

void EngineAPI::setPlayerPosition(const glm::vec3& pos) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_player) {
        m_player->Position = pos;
    }
}

glm::vec3 EngineAPI::getPlayerLookDirection() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_player) return glm::vec3(0.0f, 0.0f, -1.0f);
    return m_player->Front;
}

RaycastResult EngineAPI::getPlayerTarget(float maxDistance) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_player || !m_world) {
        RaycastResult result;
        result.hit = false;
        return result;
    }

    // Cast ray from player position in look direction
    auto hit = Raycast::castRay(m_world, m_player->Position, m_player->Front, maxDistance);

    RaycastResult result;
    result.hit = hit.hit;
    result.position = hit.position;
    result.normal = hit.normal;
    result.blockPos = glm::ivec3(hit.blockX, hit.blockY, hit.blockZ);
    result.blockID = hit.hit ? m_world->getBlockAt(static_cast<float>(hit.blockX),
                                                    static_cast<float>(hit.blockY),
                                                    static_cast<float>(hit.blockZ)) : 0;
    result.distance = hit.distance;

    return result;
}

// ============================================================================
// Water
// ============================================================================

bool EngineAPI::placeWater(const glm::ivec3& pos) {
    // Water block ID is typically 4
    return placeBlock(pos, 4);
}

bool EngineAPI::removeWater(const glm::ivec3& pos) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_world) return false;

    int blockID = m_world->getBlockAt(static_cast<float>(pos.x),
                                      static_cast<float>(pos.y),
                                      static_cast<float>(pos.z));

    // Check if it's water (block ID 4)
    if (blockID == 4) {
        return breakBlock(pos);
    }

    return false;
}

int EngineAPI::floodFill(const glm::ivec3& start, int blockID, int maxBlocks) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_world || !m_renderer) return 0;

    int count = 0;
    std::queue<glm::ivec3> queue;
    std::unordered_set<uint64_t> visited;

    // Helper to convert position to hash
    auto posToHash = [](const glm::ivec3& p) -> uint64_t {
        return (static_cast<uint64_t>(p.x) << 32) |
               (static_cast<uint64_t>(p.y) << 16) |
               static_cast<uint64_t>(p.z);
    };

    queue.push(start);
    visited.insert(posToHash(start));

    glm::ivec3 min = start;
    glm::ivec3 max = start;

    // 6 directions
    const glm::ivec3 directions[] = {
        {1, 0, 0}, {-1, 0, 0},
        {0, 1, 0}, {0, -1, 0},
        {0, 0, 1}, {0, 0, -1}
    };

    while (!queue.empty() && count < maxBlocks) {
        glm::ivec3 pos = queue.front();
        queue.pop();

        // Check if current block is air
        int current = m_world->getBlockAt(static_cast<float>(pos.x),
                                          static_cast<float>(pos.y),
                                          static_cast<float>(pos.z));

        if (current != 0) continue;  // Not air, skip

        // Place block
        m_world->setBlockAt(static_cast<float>(pos.x),
                           static_cast<float>(pos.y),
                           static_cast<float>(pos.z),
                           blockID,
                           false);
        count++;

        // Update bounds
        min = glm::min(min, pos);
        max = glm::max(max, pos);

        // Add neighbors
        for (const auto& dir : directions) {
            glm::ivec3 next = pos + dir;
            uint64_t hash = posToHash(next);

            if (visited.find(hash) == visited.end()) {
                visited.insert(hash);
                queue.push(next);
            }
        }
    }

    // Regenerate affected chunks
    if (count > 0) {
        regenerateAffectedChunks(min, max);
    }

    return count;
}

// ============================================================================
// Utility
// ============================================================================

int EngineAPI::getBlockID(const std::string& blockName) {
    return BlockRegistry::instance().getID(blockName);
}

std::string EngineAPI::getBlockName(int blockID) {
    return BlockRegistry::instance().getBlockName(blockID);
}

std::vector<std::string> EngineAPI::getAllBlockNames() {
    std::vector<std::string> names;
    auto& registry = BlockRegistry::instance();

    for (int i = 0; i < registry.count(); ++i) {
        try {
            const auto& block = registry.get(i);
            names.push_back(block.name);
        } catch (...) {
            // Skip invalid blocks
        }
    }

    return names;
}

std::vector<std::string> EngineAPI::getAllStructureNames() {
    return StructureRegistry::instance().getAllStructureNames();
}

std::vector<std::string> EngineAPI::getAllBiomeNames() {
    std::vector<std::string> names;
    auto& registry = BiomeRegistry::getInstance();

    for (int i = 0; i < registry.getBiomeCount(); ++i) {
        auto biome = registry.getBiomeByIndex(i);
        if (biome) {
            names.push_back(biome->name);
        }
    }

    return names;
}

float EngineAPI::getTimeOfDay() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_timeOfDay;
}

void EngineAPI::setTimeOfDay(float time) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_timeOfDay = std::fmod(time, 1.0f);
    if (m_timeOfDay < 0.0f) m_timeOfDay += 1.0f;
}

// ============================================================================
// Helper Functions
// ============================================================================

void EngineAPI::regenerateAffectedChunks(const glm::ivec3& start, const glm::ivec3& end) {
    if (!m_world || !m_renderer) return;

    // Calculate affected chunk range
    const int CHUNK_SIZE = 32;
    int chunkStartX = start.x / CHUNK_SIZE;
    int chunkStartY = start.y / CHUNK_SIZE;
    int chunkStartZ = start.z / CHUNK_SIZE;
    int chunkEndX = end.x / CHUNK_SIZE;
    int chunkEndY = end.y / CHUNK_SIZE;
    int chunkEndZ = end.z / CHUNK_SIZE;

    // Add margin for neighboring chunks (for face culling)
    chunkStartX -= 1;
    chunkStartY -= 1;
    chunkStartZ -= 1;
    chunkEndX += 1;
    chunkEndY += 1;
    chunkEndZ += 1;

    // Regenerate all affected chunks
    for (int cy = chunkStartY; cy <= chunkEndY; ++cy) {
        for (int cz = chunkStartZ; cz <= chunkEndZ; ++cz) {
            for (int cx = chunkStartX; cx <= chunkEndX; ++cx) {
                auto chunk = m_world->getChunkAt(cx, cy, cz);
                if (chunk) {
                    chunk->generateMesh(m_world, &m_world->getMeshBufferPool());
                    chunk->createBuffers(m_renderer);
                }
            }
        }
    }
}
