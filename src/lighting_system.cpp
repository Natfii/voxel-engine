#include "lighting_system.h"
#include "world.h"
#include "chunk.h"
#include "block_system.h"
#include "frustum.h"
#include "logger.h"
#include "vulkan_renderer.h"
#include <algorithm>
#include <iostream>

// ========== Constructor/Destructor ==========

LightingSystem::LightingSystem(World* world)
    : m_world(world) {
    if (!m_world) {
        throw std::runtime_error("LightingSystem: World pointer cannot be null");
    }
}

LightingSystem::~LightingSystem() {
    // Nothing to clean up
}

// ========== Initialization ==========

void LightingSystem::initializeWorldLighting() {
    std::cout << "Initializing world lighting..." << std::endl;

    // Get world bounds to iterate through all chunk columns
    const auto& chunks = m_world->getChunks();
    if (chunks.empty()) {
        std::cout << "No chunks to light!" << std::endl;
        return;
    }

    // Find min/max chunk coordinates
    int minChunkX = std::numeric_limits<int>::max();
    int maxChunkX = std::numeric_limits<int>::min();
    int minChunkZ = std::numeric_limits<int>::max();
    int maxChunkZ = std::numeric_limits<int>::min();

    for (Chunk* chunk : chunks) {
        minChunkX = std::min(minChunkX, chunk->getChunkX());
        maxChunkX = std::max(maxChunkX, chunk->getChunkX());
        minChunkZ = std::min(minChunkZ, chunk->getChunkZ());
        maxChunkZ = std::max(maxChunkZ, chunk->getChunkZ());
    }

    // Generate sunlight for all columns
    for (int chunkX = minChunkX; chunkX <= maxChunkX; chunkX++) {
        for (int chunkZ = minChunkZ; chunkZ <= maxChunkZ; chunkZ++) {
            generateSunlightColumn(chunkX, chunkZ);
        }
    }

    // Process all queued light propagation
    std::cout << "Processing " << m_lightAddQueue.size() << " light propagation nodes..." << std::endl;
    int processedCount = 0;
    while (!m_lightAddQueue.empty()) {
        LightNode node = m_lightAddQueue.front();
        m_lightAddQueue.pop_front();
        propagateLightStep(node);
        processedCount++;
    }

    std::cout << "World lighting initialized! Processed " << processedCount << " nodes." << std::endl;
}

// ========== Update (Incremental) ==========

void LightingSystem::update(float deltaTime, VulkanRenderer* renderer) {
    // Process light additions (new torches, sunlight spread, etc.)
    int addCount = 0;
    while (!m_lightAddQueue.empty() && addCount < MAX_LIGHT_ADDS_PER_FRAME) {
        LightNode node = m_lightAddQueue.front();
        m_lightAddQueue.pop_front();
        propagateLightStep(node);
        addCount++;
    }

    // Process light removals (higher priority - removes look worse than slow additions)
    int removeCount = 0;
    while (!m_lightRemoveQueue.empty() && removeCount < MAX_LIGHT_REMOVES_PER_FRAME) {
        LightNode node = m_lightRemoveQueue.front();
        m_lightRemoveQueue.pop_front();
        removeLightStep(node);
        removeCount++;
    }

    // Regenerate dirty chunk meshes (batched to avoid frame drops)
    if (!m_dirtyChunks.empty() && renderer != nullptr) {
        regenerateDirtyChunks(MAX_MESH_REGEN_PER_FRAME, renderer);
    }
}

// ========== Light Source Management ==========

void LightingSystem::addLightSource(const glm::vec3& worldPos, uint8_t lightLevel) {
    glm::ivec3 blockPos(
        static_cast<int>(std::floor(worldPos.x)),
        static_cast<int>(std::floor(worldPos.y)),
        static_cast<int>(std::floor(worldPos.z))
    );

    // Set the light at the source position
    setBlockLight(blockPos, lightLevel);

    // Queue for BFS propagation
    m_lightAddQueue.emplace_back(blockPos, lightLevel, false);  // false = block light
}

void LightingSystem::addSkyLightSource(const glm::vec3& worldPos, uint8_t lightLevel) {
    glm::ivec3 blockPos(
        static_cast<int>(std::floor(worldPos.x)),
        static_cast<int>(std::floor(worldPos.y)),
        static_cast<int>(std::floor(worldPos.z))
    );

    // Sky light is already set by initializeChunkLighting, just queue for propagation
    // Queue for BFS propagation
    m_lightAddQueue.emplace_back(blockPos, lightLevel, true);  // true = sky light
}

void LightingSystem::removeLightSource(const glm::vec3& worldPos) {
    glm::ivec3 blockPos(
        static_cast<int>(std::floor(worldPos.x)),
        static_cast<int>(std::floor(worldPos.y)),
        static_cast<int>(std::floor(worldPos.z))
    );

    uint8_t oldLight = getBlockLight(blockPos);
    if (oldLight > 0) {
        // Queue for removal algorithm
        m_lightRemoveQueue.emplace_back(blockPos, oldLight, false);  // false = block light
    }
}

// ========== Block Change Integration ==========

void LightingSystem::notifyChunkUnload(Chunk* chunk) {
    if (!chunk) return;

    // Remove chunk from dirty chunks to prevent dangling pointer access
    // This is CRITICAL - without this, the lighting system will crash
    // when trying to regenerate meshes for unloaded chunks
    m_dirtyChunks.erase(chunk);
}

void LightingSystem::onBlockChanged(const glm::ivec3& worldPos, bool wasOpaque, bool isOpaque) {
    // If block became transparent, sunlight may flood down
    if (wasOpaque && !isOpaque) {
        // Check if there's sunlight above
        glm::ivec3 abovePos = worldPos + glm::ivec3(0, 1, 0);
        uint8_t aboveLight = getSkyLight(abovePos);
        if (aboveLight > 0) {
            // Propagate sunlight downward
            setSkyLight(worldPos, aboveLight);
            m_lightAddQueue.emplace_back(worldPos, aboveLight, true);  // true = sky light
        }
    }
    // If block became opaque, light is blocked
    else if (!wasOpaque && isOpaque) {
        // Remove light at this position
        uint8_t oldSkyLight = getSkyLight(worldPos);
        uint8_t oldBlockLight = getBlockLight(worldPos);

        if (oldSkyLight > 0) {
            setSkyLight(worldPos, 0);
            m_lightRemoveQueue.emplace_back(worldPos, oldSkyLight, true);
        }
        if (oldBlockLight > 0) {
            setBlockLight(worldPos, 0);
            m_lightRemoveQueue.emplace_back(worldPos, oldBlockLight, false);
        }
    }
}

// ========== Light Queries ==========

uint8_t LightingSystem::getSkyLight(const glm::ivec3& worldPos) const {
    Chunk* chunk = m_world->getChunkAtWorldPos(
        static_cast<float>(worldPos.x),
        static_cast<float>(worldPos.y),
        static_cast<float>(worldPos.z)
    );

    if (!chunk) return 0;

    // Convert world position to local chunk coordinates
    int localX = worldPos.x - (chunk->getChunkX() * Chunk::WIDTH);
    int localY = worldPos.y - (chunk->getChunkY() * Chunk::HEIGHT);
    int localZ = worldPos.z - (chunk->getChunkZ() * Chunk::DEPTH);

    return chunk->getSkyLight(localX, localY, localZ);
}

uint8_t LightingSystem::getBlockLight(const glm::ivec3& worldPos) const {
    Chunk* chunk = m_world->getChunkAtWorldPos(
        static_cast<float>(worldPos.x),
        static_cast<float>(worldPos.y),
        static_cast<float>(worldPos.z)
    );

    if (!chunk) return 0;

    // Convert world position to local chunk coordinates
    int localX = worldPos.x - (chunk->getChunkX() * Chunk::WIDTH);
    int localY = worldPos.y - (chunk->getChunkY() * Chunk::HEIGHT);
    int localZ = worldPos.z - (chunk->getChunkZ() * Chunk::DEPTH);

    return chunk->getBlockLight(localX, localY, localZ);
}

uint8_t LightingSystem::getCombinedLight(const glm::ivec3& worldPos) const {
    return std::max(getSkyLight(worldPos), getBlockLight(worldPos));
}

// ========== Internal Helper Methods ==========

void LightingSystem::setSkyLight(const glm::ivec3& worldPos, uint8_t value) {
    Chunk* chunk = m_world->getChunkAtWorldPos(
        static_cast<float>(worldPos.x),
        static_cast<float>(worldPos.y),
        static_cast<float>(worldPos.z)
    );

    if (!chunk) {
        // Chunk doesn't exist yet (world streaming) - silently skip
        return;
    }

    // Convert world position to local chunk coordinates
    int localX = worldPos.x - (chunk->getChunkX() * Chunk::WIDTH);
    int localY = worldPos.y - (chunk->getChunkY() * Chunk::HEIGHT);
    int localZ = worldPos.z - (chunk->getChunkZ() * Chunk::DEPTH);

    chunk->setSkyLight(localX, localY, localZ, value);
    chunk->markLightingDirty();
    m_dirtyChunks.insert(chunk);

    // Mark neighbor chunks dirty if on boundary
    markNeighborChunksDirty(chunk, localX, localY, localZ);
}

void LightingSystem::setBlockLight(const glm::ivec3& worldPos, uint8_t value) {
    Chunk* chunk = m_world->getChunkAtWorldPos(
        static_cast<float>(worldPos.x),
        static_cast<float>(worldPos.y),
        static_cast<float>(worldPos.z)
    );

    if (!chunk) {
        // Chunk doesn't exist yet (world streaming) - silently skip
        return;
    }

    // Convert world position to local chunk coordinates
    int localX = worldPos.x - (chunk->getChunkX() * Chunk::WIDTH);
    int localY = worldPos.y - (chunk->getChunkY() * Chunk::HEIGHT);
    int localZ = worldPos.z - (chunk->getChunkZ() * Chunk::DEPTH);

    chunk->setBlockLight(localX, localY, localZ, value);
    chunk->markLightingDirty();
    m_dirtyChunks.insert(chunk);

    // Mark neighbor chunks dirty if on boundary
    markNeighborChunksDirty(chunk, localX, localY, localZ);
}

bool LightingSystem::isTransparent(const glm::ivec3& worldPos) const {
    int blockID = m_world->getBlockAt(
        static_cast<float>(worldPos.x),
        static_cast<float>(worldPos.y),
        static_cast<float>(worldPos.z)
    );

    // Air is always transparent
    if (blockID == BlockID::AIR) {
        return true;
    }

    // Check block transparency property
    try {
        const auto& blockDef = BlockRegistry::instance().get(blockID);
        return blockDef.transparency > 0.0f;
    } catch (...) {
        // Unknown block, treat as opaque
        return false;
    }
}

void LightingSystem::markNeighborChunksDirty(Chunk* chunk, int localX, int localY, int localZ) {
    // If block is on chunk boundary, mark neighbor chunks dirty
    if (localX == 0) {
        Chunk* neighbor = m_world->getChunkAt(chunk->getChunkX() - 1, chunk->getChunkY(), chunk->getChunkZ());
        if (neighbor) {
            neighbor->markLightingDirty();
            m_dirtyChunks.insert(neighbor);
        }
    } else if (localX == Chunk::WIDTH - 1) {
        Chunk* neighbor = m_world->getChunkAt(chunk->getChunkX() + 1, chunk->getChunkY(), chunk->getChunkZ());
        if (neighbor) {
            neighbor->markLightingDirty();
            m_dirtyChunks.insert(neighbor);
        }
    }

    if (localY == 0) {
        Chunk* neighbor = m_world->getChunkAt(chunk->getChunkX(), chunk->getChunkY() - 1, chunk->getChunkZ());
        if (neighbor) {
            neighbor->markLightingDirty();
            m_dirtyChunks.insert(neighbor);
        }
    } else if (localY == Chunk::HEIGHT - 1) {
        Chunk* neighbor = m_world->getChunkAt(chunk->getChunkX(), chunk->getChunkY() + 1, chunk->getChunkZ());
        if (neighbor) {
            neighbor->markLightingDirty();
            m_dirtyChunks.insert(neighbor);
        }
    }

    if (localZ == 0) {
        Chunk* neighbor = m_world->getChunkAt(chunk->getChunkX(), chunk->getChunkY(), chunk->getChunkZ() - 1);
        if (neighbor) {
            neighbor->markLightingDirty();
            m_dirtyChunks.insert(neighbor);
        }
    } else if (localZ == Chunk::DEPTH - 1) {
        Chunk* neighbor = m_world->getChunkAt(chunk->getChunkX(), chunk->getChunkY(), chunk->getChunkZ() + 1);
        if (neighbor) {
            neighbor->markLightingDirty();
            m_dirtyChunks.insert(neighbor);
        }
    }
}

void LightingSystem::regenerateDirtyChunks(int maxPerFrame, VulkanRenderer* renderer) {
    int regenerated = 0;
    auto it = m_dirtyChunks.begin();

    while (it != m_dirtyChunks.end() && regenerated < maxPerFrame) {
        Chunk* chunk = *it;

        // LIGHTING FIX: Actually regenerate mesh with updated lighting values
        try {
            // Regenerate mesh with new lighting data
            chunk->generateMesh(m_world, false);  // Don't hold lock

            // Upload mesh to GPU (async to prevent frame stalls)
            if (renderer != nullptr) {
                renderer->beginAsyncChunkUpload();
                chunk->createVertexBufferBatched(renderer);
                renderer->submitAsyncChunkUpload(chunk);
            }

            // Clear the dirty flag
            chunk->clearLightingDirty();

            it = m_dirtyChunks.erase(it);
            regenerated++;
        } catch (const std::exception& e) {
            // Log error but continue processing other chunks
            Logger::error() << "Failed to regenerate chunk mesh for lighting: " << e.what();
            it = m_dirtyChunks.erase(it);  // Remove from queue anyway to prevent infinite retry
        }
    }
}

// ========== Phase 2: Sunlight Generation ==========

void LightingSystem::generateSunlightColumn(int chunkX, int chunkZ) {
    // For each (x, z) position within the chunk, find the highest solid block
    // and propagate sunlight downward

    for (int localX = 0; localX < Chunk::WIDTH; localX++) {
        for (int localZ = 0; localZ < Chunk::DEPTH; localZ++) {
            // Convert to world coordinates
            int worldX = chunkX * Chunk::WIDTH + localX;
            int worldZ = chunkZ * Chunk::DEPTH + localZ;

            // Find the highest non-air block in this column
            // Start from top of world and work down
            bool foundSurface = false;
            int maxY = 320;  // Assuming world height limit

            for (int worldY = maxY; worldY >= 0; worldY--) {
                glm::ivec3 blockPos(worldX, worldY, worldZ);
                int blockID = m_world->getBlockAt(
                    static_cast<float>(worldX),
                    static_cast<float>(worldY),
                    static_cast<float>(worldZ)
                );

                if (!foundSurface) {
                    // Above surface - set full sunlight (15)
                    if (blockID == BlockID::AIR) {
                        setSkyLight(blockPos, 15);
                        // Queue for horizontal propagation
                        m_lightAddQueue.emplace_back(blockPos, 15, true);
                    } else {
                        // Found surface - check if transparent
                        if (isTransparent(blockPos)) {
                            // Transparent block - sunlight continues down with full strength
                            setSkyLight(blockPos, 15);
                            // CRITICAL: Queue transparent blocks for horizontal propagation!
                            m_lightAddQueue.emplace_back(blockPos, 15, true);
                        } else {
                            // Opaque block - sunlight stops
                            foundSurface = true;
                            setSkyLight(blockPos, 0);
                        }
                    }
                } else {
                    // Below surface - no sunlight
                    setSkyLight(blockPos, 0);
                }
            }
        }
    }
}

// ========== Phase 2 & 3: BFS Light Propagation ==========

void LightingSystem::propagateLightStep(const LightNode& node) {
    // Check if chunk exists for this node position
    Chunk* chunk = m_world->getChunkAtWorldPos(
        static_cast<float>(node.position.x),
        static_cast<float>(node.position.y),
        static_cast<float>(node.position.z)
    );

    if (!chunk) {
        // Chunk doesn't exist - skip this light propagation
        return;
    }

    // Get current light level at this position
    uint8_t currentLight = node.isSkyLight ? getSkyLight(node.position) : getBlockLight(node.position);

    // Sanity check - if light level doesn't match node, skip
    if (currentLight != node.lightLevel) {
        return;
    }

    // Don't propagate if light is too dim
    if (currentLight <= 1) {
        return;
    }

    // Propagate to all 6 neighbors
    const glm::ivec3 neighbors[6] = {
        node.position + glm::ivec3(1, 0, 0),   // +X
        node.position + glm::ivec3(-1, 0, 0),  // -X
        node.position + glm::ivec3(0, 1, 0),   // +Y
        node.position + glm::ivec3(0, -1, 0),  // -Y
        node.position + glm::ivec3(0, 0, 1),   // +Z
        node.position + glm::ivec3(0, 0, -1)   // -Z
    };

    for (int i = 0; i < 6; i++) {
        const glm::ivec3& neighborPos = neighbors[i];

        // Check if neighbor chunk exists
        Chunk* neighborChunk = m_world->getChunkAtWorldPos(
            static_cast<float>(neighborPos.x),
            static_cast<float>(neighborPos.y),
            static_cast<float>(neighborPos.z)
        );

        if (!neighborChunk) {
            // Neighbor chunk doesn't exist - skip this neighbor
            continue;
        }

        // Skip if opaque
        if (!isTransparent(neighborPos)) {
            continue;
        }

        // Calculate new light level
        uint8_t newLight;
        if (node.isSkyLight && i == 3) {
            // Sunlight going straight down (neighbor -Y) - no decay
            newLight = currentLight;
        } else {
            // All other directions - decay by 1
            newLight = currentLight - 1;
        }

        // Get current light at neighbor
        uint8_t neighborLight = node.isSkyLight ? getSkyLight(neighborPos) : getBlockLight(neighborPos);

        // Only update if new light is brighter
        if (newLight > neighborLight) {
            if (node.isSkyLight) {
                setSkyLight(neighborPos, newLight);
            } else {
                setBlockLight(neighborPos, newLight);
            }

            // Queue neighbor for further propagation
            m_lightAddQueue.emplace_back(neighborPos, newLight, node.isSkyLight);
        }
    }
}

// ========== Phase 4: Light Removal (Two-Queue Algorithm) ==========

void LightingSystem::removeLightStep(const LightNode& node) {
    /**
     * Two-Queue Light Removal Algorithm:
     *
     * Phase 1: Clear affected area
     * - Remove light at source position
     * - For each neighbor:
     *   - If neighbor's light < removed light AND > 0: queue for removal (propagate darkness)
     *   - If neighbor's light >= removed light: queue for re-propagation (it's a light source)
     *
     * Phase 2: Re-propagate valid light
     * - Process the re-propagation queue to restore light from remaining sources
     */

    std::deque<LightNode> removalQueue;
    std::deque<LightNode> addBackQueue;

    // Start with the removed node
    removalQueue.push_back(node);

    // Phase 1: Collect all blocks affected by this light removal
    while (!removalQueue.empty()) {
        LightNode currentNode = removalQueue.front();
        removalQueue.pop_front();

        uint8_t oldLight = currentNode.lightLevel;

        // Clear light at this position
        if (currentNode.isSkyLight) {
            setSkyLight(currentNode.position, 0);
        } else {
            setBlockLight(currentNode.position, 0);
        }

        // Check all 6 neighbors
        const glm::ivec3 neighbors[6] = {
            currentNode.position + glm::ivec3(1, 0, 0),
            currentNode.position + glm::ivec3(-1, 0, 0),
            currentNode.position + glm::ivec3(0, 1, 0),
            currentNode.position + glm::ivec3(0, -1, 0),
            currentNode.position + glm::ivec3(0, 0, 1),
            currentNode.position + glm::ivec3(0, 0, -1)
        };

        for (const auto& neighborPos : neighbors) {
            uint8_t neighborLight = currentNode.isSkyLight ?
                getSkyLight(neighborPos) : getBlockLight(neighborPos);

            if (neighborLight == 0) {
                // No light here, skip
                continue;
            }

            if (neighborLight < oldLight) {
                // This light came from the removed source - remove it too
                removalQueue.emplace_back(neighborPos, neighborLight, currentNode.isSkyLight);
            } else if (neighborLight >= oldLight) {
                // This is a light source or received light from elsewhere
                // Queue for re-propagation to fill in the gaps
                addBackQueue.emplace_back(neighborPos, neighborLight, currentNode.isSkyLight);
            }
        }
    }

    // Phase 2: Re-propagate light from remaining sources
    // Add all remaining light sources back to the main propagation queue
    while (!addBackQueue.empty()) {
        m_lightAddQueue.push_back(addBackQueue.front());
        addBackQueue.pop_front();
    }
}

// ========== Viewport-Based Lighting ==========

std::vector<Chunk*> LightingSystem::getVisibleChunks(const Frustum& frustum) const {
    std::vector<Chunk*> visibleChunks;

    const auto& allChunks = m_world->getChunks();
    for (Chunk* chunk : allChunks) {
        if (!chunk) continue;

        // Use same frustum test as rendering
        glm::vec3 chunkMin = chunk->getMin();
        glm::vec3 chunkMax = chunk->getMax();

        if (frustumAABBIntersect(frustum, chunkMin, chunkMax, 2.0f)) {
            visibleChunks.push_back(chunk);
        }
    }

    return visibleChunks;
}

void LightingSystem::recalculateViewportLighting(const Frustum& frustum, const glm::vec3& playerPos) {
    // Get visible chunks only
    std::vector<Chunk*> visibleChunks = getVisibleChunks(frustum);

    Logger::info() << "Recalculating lighting for " << visibleChunks.size() << " visible chunks";

    // Reinitialize sky light for visible chunks
    for (Chunk* chunk : visibleChunks) {
        m_world->initializeChunkLighting(chunk);
        chunk->markLightingDirty();
        m_dirtyChunks.insert(chunk);
    }

    // Propagate lighting (BFS flood-fill)
    int propagated = 0;
    while (!m_lightAddQueue.empty()) {
        LightNode node = m_lightAddQueue.front();
        m_lightAddQueue.pop_front();
        propagateLightStep(node);
        propagated++;
    }

    Logger::info() << "Viewport lighting recalculation complete (propagated " << propagated << " light nodes)";
}
