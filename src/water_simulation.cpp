/**
 * @file water_simulation.cpp
 * @brief Minecraft-style cellular automata water simulation
 *
 * Water uses simple cellular automata (DwarfCorp-inspired):
 * - Each frame, iterate through all water cells
 * - Each cell tries to flow down first, then spread horizontally
 * - Maintains Minecraft's 7-block max flow distance (levels 8→1)
 */

#include "water_simulation.h"
#include "world.h"
#include "world_utils.h"
#include "block_system.h"
#include "chunk.h"
#include "terrain_constants.h"
#include "logger.h"
#include "debug_state.h"
#include <algorithm>
#include <array>

WaterSimulation::WaterSimulation() {
}

WaterSimulation::~WaterSimulation() {
}

// ============================================================================
// Main Update - Cellular Automata
// ============================================================================

void WaterSimulation::update(float deltaTime, World* world, const glm::vec3& playerPos, float renderDistance) {
    (void)deltaTime;
    (void)playerPos;
    (void)renderDistance;

    if (!world) return;

    constexpr int BLOCK_WATER = 5;

    // Process multiple iterations per frame to make water flow faster
    // Using 2 iterations to balance speed vs chunk rebuild limits (10/frame)
    // More iterations = more dirty chunks = flashing as chunks can't rebuild fast enough
    constexpr int MAX_ITERATIONS = 2;

    for (int iteration = 0; iteration < MAX_ITERATIONS; iteration++) {
        // Collect cells to update (can't modify while iterating)
        std::vector<glm::ivec3> cellsToProcess;
        for (const auto& [pos, cell] : m_waterCells) {
            if (cell.level > 0) {
                cellsToProcess.push_back(pos);
            }
        }

        if (cellsToProcess.empty()) break;

        // Debug: Show active water cells count periodically (debug builds only)
#ifndef NDEBUG
        static int frameCount = 0;
        ++frameCount;

        if (DebugState::instance().debugWater.getValue() && frameCount % 60 == 0) {
            Logger::debug() << "Water simulation: " << cellsToProcess.size() << " active cells";
        }
#endif

        // Track if any changes occurred this iteration
        bool anyChanges = false;

        // Process each water cell (cellular automata style)
        for (const auto& pos : cellsToProcess) {
            auto it = m_waterCells.find(pos);
            if (it == m_waterCells.end() || it->second.level == 0) continue;

            uint8_t currentLevel = it->second.level;

            // Step 1: Try to flow DOWN (falling water is always full level)
            glm::ivec3 below = pos - glm::ivec3(0, 1, 0);
            int belowBlock = world->getBlockAt(below.x, below.y, below.z);
            if (belowBlock == 0) {  // Air below
                // Check if we already have water there
                auto belowIt = m_waterCells.find(below);
                if (belowIt == m_waterCells.end() || belowIt->second.level < LEVEL_SOURCE) {
                    // Place water below at full level (falling water)
                    world->setBlockAt(static_cast<float>(below.x), static_cast<float>(below.y),
                                     static_cast<float>(below.z), BLOCK_WATER, false);

                    WaterCell& belowCell = m_waterCells[below];
                    belowCell.level = LEVEL_SOURCE;  // Falling water is full
                    belowCell.isSource = false;
                    belowCell.fluidType = 1;

                    syncToChunk(below, LEVEL_SOURCE, world);
                    markChunkDirtyAt(below);
                    markChunkDirtyAt(pos);  // Source chunk also needs update for faces
                    anyChanges = true;
                }
            }

            // Step 2: Spread horizontally (only if level > 1)
            if (currentLevel > LEVEL_MIN_FLOW) {
                uint8_t spreadLevel = currentLevel - 1;

                std::array<glm::ivec3, 4> neighbors = {{
                    pos + glm::ivec3(1, 0, 0),
                    pos + glm::ivec3(-1, 0, 0),
                    pos + glm::ivec3(0, 0, 1),
                    pos + glm::ivec3(0, 0, -1)
                }};

                for (const auto& neighbor : neighbors) {
                    int neighborBlock = world->getBlockAt(neighbor.x, neighbor.y, neighbor.z);
                    if (neighborBlock != 0) continue;  // Only spread into air

                    // Check if we already have water there at higher/equal level
                    auto neighborIt = m_waterCells.find(neighbor);
                    if (neighborIt != m_waterCells.end() && neighborIt->second.level >= spreadLevel) {
                        continue;  // Already has enough water
                    }

                    // Place flowing water
                    world->setBlockAt(static_cast<float>(neighbor.x), static_cast<float>(neighbor.y),
                                     static_cast<float>(neighbor.z), BLOCK_WATER, false);

                    WaterCell& neighborCell = m_waterCells[neighbor];
                    neighborCell.level = spreadLevel;
                    neighborCell.isSource = false;
                    neighborCell.fluidType = 1;

                    syncToChunk(neighbor, spreadLevel, world);
                    markChunkDirtyAt(neighbor);
                    markChunkDirtyAt(pos);  // Source chunk also needs update for faces
                    anyChanges = true;
                }
            }
        }

        // If no changes this iteration, stop early
        if (!anyChanges) break;
    }

    // Update active chunks set
    m_activeChunks.clear();
    for (const auto& [pos, cell] : m_waterCells) {
        if (cell.level > 0) {
            m_activeChunks.insert(worldToChunk(pos));
        }
    }
}

// ============================================================================
// Water Placement/Removal
// ============================================================================

void WaterSimulation::placeWaterSource(int x, int y, int z, World* world) {
    glm::ivec3 pos(x, y, z);

    // Can't place water in solid block
    if (isBlockSolid(x, y, z, world)) {
        return;
    }

    // Set as source block
    setWaterCell(pos, LEVEL_SOURCE, true, world);
    m_sourceBlocks.insert(pos);

    // Queue for spreading
    queueSpread(pos);
}

void WaterSimulation::removeWaterSource(int x, int y, int z, World* world) {
    glm::ivec3 pos(x, y, z);

    // Remove source status
    m_sourceBlocks.erase(pos);

    auto it = m_waterCells.find(pos);
    if (it != m_waterCells.end()) {
        it->second.isSource = false;
    }

    // Remove the water at this position
    removeWaterCell(pos, world);

    // Queue neighbors for removal check
    std::array<glm::ivec3, 6> neighbors = {{
        pos + glm::ivec3(1, 0, 0),
        pos + glm::ivec3(-1, 0, 0),
        pos + glm::ivec3(0, 1, 0),
        pos + glm::ivec3(0, -1, 0),
        pos + glm::ivec3(0, 0, 1),
        pos + glm::ivec3(0, 0, -1)
    }};

    for (const auto& neighbor : neighbors) {
        if (m_waterCells.find(neighbor) != m_waterCells.end()) {
            queueRemove(neighbor);
        }
    }
}

bool WaterSimulation::isSource(int x, int y, int z) const {
    return m_sourceBlocks.find(glm::ivec3(x, y, z)) != m_sourceBlocks.end();
}

bool WaterSimulation::isNaturalWater(int y) const {
    // Water at or below sea level (62) is "natural" ocean/lake water
    // This is treated as infinite source - Minecraft heightmap approach
    return y <= TerrainGeneration::WATER_LEVEL;
}

void WaterSimulation::triggerWaterFlow(int brokenX, int brokenY, int brokenZ, World* world, bool lockHeld) {
    if (!world) return;

    std::cerr << "[DEBUG] triggerWaterFlow called at (" << brokenX << ", " << brokenY << ", " << brokenZ << ")" << std::endl;

    constexpr int BLOCK_WATER = 5;
    glm::ivec3 brokenPos(brokenX, brokenY, brokenZ);

    // Helper lambdas to use safe or unsafe methods based on lockHeld
    // IMPORTANT: When lockHeld=true, caller already holds World's chunk mutex
    // Using the locking versions would cause deadlock (recursive lock on shared_mutex)
    auto getBlock = [world, lockHeld](int x, int y, int z) -> int {
        float fx = static_cast<float>(x);
        float fy = static_cast<float>(y);
        float fz = static_cast<float>(z);
        return lockHeld ? world->getBlockAtUnsafe(fx, fy, fz)
                       : world->getBlockAt(fx, fy, fz);
    };

    auto setBlock = [world, lockHeld](int x, int y, int z, int blockID) {
        float fx = static_cast<float>(x);
        float fy = static_cast<float>(y);
        float fz = static_cast<float>(z);
        if (lockHeld) {
            world->setBlockAtUnsafe(fx, fy, fz, blockID);
        } else {
            world->setBlockAt(fx, fy, fz, blockID, false);
        }
    };

    // Check all 6 adjacent blocks for water
    std::array<glm::ivec3, 6> neighbors = {{
        glm::ivec3(brokenX + 1, brokenY, brokenZ),
        glm::ivec3(brokenX - 1, brokenY, brokenZ),
        glm::ivec3(brokenX, brokenY, brokenZ + 1),
        glm::ivec3(brokenX, brokenY, brokenZ - 1),
        glm::ivec3(brokenX, brokenY + 1, brokenZ),
        glm::ivec3(brokenX, brokenY - 1, brokenZ)
    }};

    // Find the highest level adjacent water source
    uint8_t bestLevel = 0;
    bool foundWater = false;

    for (const auto& neighbor : neighbors) {
        int blockID = getBlock(neighbor.x, neighbor.y, neighbor.z);
        if (blockID != BLOCK_WATER) continue;

        foundWater = true;
        uint8_t neighborLevel = LEVEL_SOURCE;  // Default for natural/untracked water

        // Check for natural water (at/below sea level) - treat as source
        if (isNaturalWater(neighbor.y)) {
            // Register this natural water block as source if not already tracked
            if (m_waterCells.find(neighbor) == m_waterCells.end()) {
                WaterCell& cell = m_waterCells[neighbor];
                cell.level = LEVEL_SOURCE;
                cell.isSource = true;
                cell.fluidType = 1;
                m_sourceBlocks.insert(neighbor);
            }
            neighborLevel = LEVEL_SOURCE;
        } else {
            // Check tracked simulation water
            auto it = m_waterCells.find(neighbor);
            if (it != m_waterCells.end()) {
                neighborLevel = it->second.level;
            }
        }

        if (neighborLevel > bestLevel) {
            bestLevel = neighborLevel;
        }
    }

    // IMMEDIATELY place water at the broken block position if adjacent to water
    if (foundWater && bestLevel > LEVEL_MIN_FLOW) {
        // Calculate the level for the new water (one less than source, or full if falling)
        uint8_t newLevel;

        // Check if water is falling from above
        int aboveBlock = getBlock(brokenX, brokenY + 1, brokenZ);
        if (aboveBlock == BLOCK_WATER) {
            newLevel = LEVEL_SOURCE;  // Falling water is full
        } else {
            newLevel = bestLevel - 1;  // Horizontal flow decreases level
            if (newLevel < LEVEL_MIN_FLOW) newLevel = LEVEL_MIN_FLOW;
        }

        // Place water at the broken block position
        setBlock(brokenX, brokenY, brokenZ, BLOCK_WATER);

        // Track in simulation
        WaterCell& newCell = m_waterCells[brokenPos];
        newCell.level = newLevel;
        newCell.isSource = false;
        newCell.fluidType = 1;

        // CRITICAL: Queue for spread so water continues flowing!
        queueSpread(brokenPos);

        // Sync to chunk and mark dirty (pass lockHeld through to avoid deadlock)
        Logger::info() << "WATER FLOW: Placing water at (" << brokenX << "," << brokenY << "," << brokenZ
                       << ") level=" << (int)newLevel << " visualLevel=" << (int)(LEVEL_SOURCE - newLevel);
        syncToChunk(brokenPos, newLevel, world, lockHeld);
        markChunkDirtyAt(brokenPos);

        // Also mark adjacent chunks dirty for face updates
        for (const auto& neighbor : neighbors) {
            markChunkDirtyAt(neighbor);
        }
    }
}

uint8_t WaterSimulation::getWaterLevel(int x, int y, int z) const {
    auto it = m_waterCells.find(glm::ivec3(x, y, z));
    return (it != m_waterCells.end()) ? it->second.level : 0;
}

uint8_t WaterSimulation::getFluidType(int x, int y, int z) const {
    auto it = m_waterCells.find(glm::ivec3(x, y, z));
    return (it != m_waterCells.end()) ? it->second.fluidType : 0;
}

glm::vec2 WaterSimulation::getFlowVector(int x, int y, int z) const {
    auto it = m_waterCells.find(glm::ivec3(x, y, z));
    return (it != m_waterCells.end()) ? it->second.flowDir : glm::vec2(0.0f);
}

// ============================================================================
// BFS Spread
// ============================================================================

void WaterSimulation::bfsSpread(World* world, int maxIterations) {
    int iterations = 0;

    while (!m_spreadQueue.empty() && iterations < maxIterations) {
        glm::ivec3 pos = m_spreadQueue.front();
        m_spreadQueue.pop();
        m_spreadQueued.erase(pos);
        iterations++;

        auto it = m_waterCells.find(pos);
        if (it == m_waterCells.end() || it->second.level == 0) {
            continue;  // No water here anymore
        }

        uint8_t currentLevel = it->second.level;
        uint8_t fluidType = it->second.fluidType;

        // ========== Step 1: Try to flow downward ==========
        glm::ivec3 below = pos - glm::ivec3(0, 1, 0);
        if (canWaterFlowTo(below.x, below.y, below.z, world)) {
            auto belowIt = m_waterCells.find(below);
            uint8_t belowLevel = (belowIt != m_waterCells.end()) ? belowIt->second.level : 0;

            // Falling water is always level 8 (like Minecraft)
            if (belowLevel < LEVEL_SOURCE) {
                setWaterCell(below, LEVEL_SOURCE, false, world);
                // Update flow direction (downward)
                m_waterCells[below].flowDir = glm::vec2(0.0f, -1.0f);
                queueSpread(below);
            }
        }

        // ========== Step 2: Spread horizontally ==========
        if (currentLevel <= LEVEL_MIN_FLOW) {
            continue;  // Can't spread further horizontally
        }

        uint8_t spreadLevel = currentLevel - 1;

        // Find path to drop (Minecraft optimization)
        glm::ivec2 dropDir = findPathToDrop(pos, world);

        // Define horizontal neighbors
        std::array<glm::ivec3, 4> horizontalNeighbors = {{
            pos + glm::ivec3(1, 0, 0),
            pos + glm::ivec3(-1, 0, 0),
            pos + glm::ivec3(0, 0, 1),
            pos + glm::ivec3(0, 0, -1)
        }};

        std::array<glm::vec2, 4> flowDirs = {{
            glm::vec2(1.0f, 0.0f),
            glm::vec2(-1.0f, 0.0f),
            glm::vec2(0.0f, 1.0f),
            glm::vec2(0.0f, -1.0f)
        }};

        for (int i = 0; i < 4; i++) {
            glm::ivec3 neighbor = horizontalNeighbors[i];

            if (!canWaterFlowTo(neighbor.x, neighbor.y, neighbor.z, world)) {
                continue;
            }

            auto neighborIt = m_waterCells.find(neighbor);
            uint8_t neighborLevel = (neighborIt != m_waterCells.end()) ? neighborIt->second.level : 0;

            // Only spread if neighbor has less water
            if (neighborLevel < spreadLevel) {
                // If there's a path to drop, prioritize that direction
                if (dropDir != glm::ivec2(0, 0)) {
                    // Check if this neighbor is in the drop direction
                    glm::ivec2 neighborDir(neighbor.x - pos.x, neighbor.z - pos.z);
                    if (neighborDir != dropDir) {
                        // Not toward drop - reduce spread level by 1 more
                        if (spreadLevel > LEVEL_MIN_FLOW + 1) {
                            // Spread but at lower level
                            uint8_t reducedLevel = spreadLevel - 1;
                            if (neighborLevel < reducedLevel) {
                                setWaterCell(neighbor, reducedLevel, false, world);
                                m_waterCells[neighbor].flowDir = flowDirs[i];
                                m_waterCells[neighbor].fluidType = fluidType;
                                queueSpread(neighbor);
                            }
                        }
                        continue;
                    }
                }

                setWaterCell(neighbor, spreadLevel, false, world);
                m_waterCells[neighbor].flowDir = flowDirs[i];
                m_waterCells[neighbor].fluidType = fluidType;
                queueSpread(neighbor);
            }
        }
    }
}

// ============================================================================
// BFS Remove
// ============================================================================

void WaterSimulation::bfsRemove(World* world, int maxIterations) {
    int iterations = 0;

    while (!m_removeQueue.empty() && iterations < maxIterations) {
        glm::ivec3 pos = m_removeQueue.front();
        m_removeQueue.pop();
        m_removeQueued.erase(pos);
        iterations++;

        auto it = m_waterCells.find(pos);
        if (it == m_waterCells.end() || it->second.level == 0) {
            continue;  // Already removed
        }

        // Source blocks are never removed by BFS
        if (it->second.isSource) {
            continue;
        }

        // Check if this water still has valid upstream
        if (hasValidUpstream(pos)) {
            continue;  // Still connected to source, don't remove
        }

        // Remove this water cell
        removeWaterCell(pos, world);

        // Queue neighbors for removal check
        std::array<glm::ivec3, 6> neighbors = {{
            pos + glm::ivec3(1, 0, 0),
            pos + glm::ivec3(-1, 0, 0),
            pos + glm::ivec3(0, 1, 0),
            pos + glm::ivec3(0, -1, 0),
            pos + glm::ivec3(0, 0, 1),
            pos + glm::ivec3(0, 0, -1)
        }};

        for (const auto& neighbor : neighbors) {
            auto neighborIt = m_waterCells.find(neighbor);
            if (neighborIt != m_waterCells.end() && !neighborIt->second.isSource) {
                queueRemove(neighbor);
            }
        }
    }
}

// ============================================================================
// Path to Drop (Minecraft optimization)
// ============================================================================

glm::ivec2 WaterSimulation::findPathToDrop(const glm::ivec3& pos, World* world) {
    // BFS to find nearest edge/drop within DROP_SEARCH_RADIUS blocks
    // Returns direction to flow toward the drop

    struct SearchNode {
        glm::ivec3 pos;
        glm::ivec2 firstDir;  // Direction of first step from origin
        int dist;
    };

    std::queue<SearchNode> searchQueue;
    std::unordered_set<glm::ivec3> visited;

    // Start with horizontal neighbors
    std::array<glm::ivec3, 4> dirs = {{
        glm::ivec3(1, 0, 0),
        glm::ivec3(-1, 0, 0),
        glm::ivec3(0, 0, 1),
        glm::ivec3(0, 0, -1)
    }};

    for (const auto& dir : dirs) {
        glm::ivec3 neighbor = pos + dir;
        if (canWaterFlowTo(neighbor.x, neighbor.y, neighbor.z, world)) {
            searchQueue.push({neighbor, glm::ivec2(dir.x, dir.z), 1});
            visited.insert(neighbor);
        }
    }

    while (!searchQueue.empty()) {
        SearchNode node = searchQueue.front();
        searchQueue.pop();

        // Check if there's a drop below this position
        glm::ivec3 below = node.pos - glm::ivec3(0, 1, 0);
        if (canWaterFlowTo(below.x, below.y, below.z, world)) {
            // Found a drop! Return direction to flow
            return node.firstDir;
        }

        // Continue searching if within radius
        if (node.dist >= DROP_SEARCH_RADIUS) {
            continue;
        }

        for (const auto& dir : dirs) {
            glm::ivec3 next = node.pos + dir;
            if (visited.find(next) == visited.end() &&
                canWaterFlowTo(next.x, next.y, next.z, world)) {
                visited.insert(next);
                searchQueue.push({next, node.firstDir, node.dist + 1});
            }
        }
    }

    // No drop found
    return glm::ivec2(0, 0);
}

// ============================================================================
// Upstream Check
// ============================================================================

bool WaterSimulation::hasValidUpstream(const glm::ivec3& pos) const {
    auto it = m_waterCells.find(pos);
    if (it == m_waterCells.end()) return false;

    uint8_t currentLevel = it->second.level;

    // Source blocks are always valid
    if (it->second.isSource) return true;

    // Check above (water falling from above)
    glm::ivec3 above = pos + glm::ivec3(0, 1, 0);
    auto aboveIt = m_waterCells.find(above);
    if (aboveIt != m_waterCells.end() && aboveIt->second.level > 0) {
        return true;  // Fed by water above
    }

    // Check horizontal neighbors for higher water
    std::array<glm::ivec3, 4> neighbors = {{
        pos + glm::ivec3(1, 0, 0),
        pos + glm::ivec3(-1, 0, 0),
        pos + glm::ivec3(0, 0, 1),
        pos + glm::ivec3(0, 0, -1)
    }};

    for (const auto& neighbor : neighbors) {
        auto neighborIt = m_waterCells.find(neighbor);
        if (neighborIt != m_waterCells.end()) {
            // Neighbor has higher level (would flow to us)
            if (neighborIt->second.level > currentLevel) {
                return true;
            }
            // Neighbor is a source at same level or higher
            if (neighborIt->second.isSource && neighborIt->second.level >= currentLevel) {
                return true;
            }
        }
    }

    return false;
}

// ============================================================================
// Helper Methods
// ============================================================================

void WaterSimulation::queueSpread(const glm::ivec3& pos) {
    if (m_spreadQueued.find(pos) == m_spreadQueued.end()) {
        m_spreadQueue.push(pos);
        m_spreadQueued.insert(pos);
    }
}

void WaterSimulation::queueRemove(const glm::ivec3& pos) {
    if (m_removeQueued.find(pos) == m_removeQueued.end()) {
        m_removeQueue.push(pos);
        m_removeQueued.insert(pos);
    }
}

void WaterSimulation::setWaterCell(const glm::ivec3& pos, uint8_t level, bool isSource, World* world) {
    WaterCell& cell = m_waterCells[pos];
    cell.level = level;
    cell.isSource = isSource;
    if (isSource) {
        m_sourceBlocks.insert(pos);
    }

    syncToChunk(pos, level, world);
    markChunkDirtyAt(pos);
}

void WaterSimulation::removeWaterCell(const glm::ivec3& pos, World* world) {
    m_waterCells.erase(pos);
    m_sourceBlocks.erase(pos);

    syncToChunk(pos, 0, world);
    markChunkDirtyAt(pos);
}

void WaterSimulation::syncToChunk(const glm::ivec3& pos, uint8_t level, World* world, bool lockHeld) {
    if (!world) return;

    // =========================================================================
    // WATER BLOCK SYNC: Place water blocks and set metadata
    // =========================================================================

    float worldX = static_cast<float>(pos.x);
    float worldY = static_cast<float>(pos.y);
    float worldZ = static_cast<float>(pos.z);

    // BLOCK_WATER = 5 (from terrain_constants.h)
    constexpr int BLOCK_WATER = 5;

    // Convert level (0-8) to visual height (0-7)
    // Level 8 = full water = visual 0
    // Level 1 = minimum = visual 7
    uint8_t visualLevel = (level >= LEVEL_SOURCE) ? 0 : (LEVEL_SOURCE - level);
    if (visualLevel > 7) visualLevel = 7;

    if (level > 0) {
        // Place water block if not already water
        int currentBlock = lockHeld ? world->getBlockAtUnsafe(worldX, worldY, worldZ)
                                    : world->getBlockAt(worldX, worldY, worldZ);
        if (currentBlock == 0) {  // Only place water in air blocks
            if (lockHeld) {
                world->setBlockAtUnsafe(worldX, worldY, worldZ, BLOCK_WATER);
            } else {
                world->setBlockAt(worldX, worldY, worldZ, BLOCK_WATER, false);
            }
        }

        // Set metadata using World's method (handles coordinates correctly)
        if (lockHeld) {
            world->setBlockMetadataAtUnsafe(worldX, worldY, worldZ, visualLevel);
        } else {
            world->setBlockMetadataAt(worldX, worldY, worldZ, visualLevel);
        }

        std::cerr << "[DEBUG] syncToChunk: Set metadata at (" << pos.x << "," << pos.y << "," << pos.z
                  << ") level=" << (int)level << " visualLevel=" << (int)visualLevel << std::endl;
    }
}

void WaterSimulation::markChunkDirtyAt(const glm::ivec3& pos) {
    glm::ivec3 chunkPos = worldToChunk(pos);
    m_dirtyChunks.insert(chunkPos);
}

bool WaterSimulation::isBlockSolid(int x, int y, int z, World* world) const {
    if (!world) return true;

    int blockID = world->getBlockAt(x, y, z);
    if (blockID == 0) return false;  // Air

    auto& registry = BlockRegistry::instance();
    if (blockID < 0 || blockID >= registry.count()) return false;

    const BlockDefinition& blockDef = registry.get(blockID);
    return !blockDef.isLiquid;
}

bool WaterSimulation::canWaterFlowTo(int x, int y, int z, World* world) const {
    if (!world) return false;

    int blockID = world->getBlockAt(x, y, z);

    // Can only flow into air - not into existing water (prevents infinite spread into ocean)
    return blockID == 0;
}

glm::ivec3 WaterSimulation::worldToChunk(const glm::ivec3& worldPos) const {
    // Use arithmetic right shift for correct negative handling
    return glm::ivec3(
        worldPos.x >> 5,  // Divide by 32
        worldPos.y >> 5,
        worldPos.z >> 5
    );
}

// ============================================================================
// Chunk Lifecycle
// ============================================================================

void WaterSimulation::notifyChunkUnload(int chunkX, int chunkY, int chunkZ) {
    int minX = chunkX * 32;
    int minY = chunkY * 32;
    int minZ = chunkZ * 32;
    int maxX = minX + 32;
    int maxY = minY + 32;
    int maxZ = minZ + 32;

    // Remove water cells in this chunk
    for (auto it = m_waterCells.begin(); it != m_waterCells.end();) {
        const glm::ivec3& pos = it->first;
        if (pos.x >= minX && pos.x < maxX &&
            pos.y >= minY && pos.y < maxY &&
            pos.z >= minZ && pos.z < maxZ) {
            m_sourceBlocks.erase(pos);
            m_spreadQueued.erase(pos);
            m_removeQueued.erase(pos);
            it = m_waterCells.erase(it);
        } else {
            ++it;
        }
    }
}

void WaterSimulation::notifyChunkUnloadBatch(const std::vector<std::tuple<int, int, int>>& chunks) {
    if (chunks.empty()) return;

    std::unordered_set<glm::ivec3> chunkSet;
    for (const auto& [chunkX, chunkY, chunkZ] : chunks) {
        chunkSet.insert(glm::ivec3(chunkX, chunkY, chunkZ));
    }

    for (auto it = m_waterCells.begin(); it != m_waterCells.end();) {
        const glm::ivec3& pos = it->first;
        glm::ivec3 cellChunk = worldToChunk(pos);

        if (chunkSet.find(cellChunk) != chunkSet.end()) {
            m_sourceBlocks.erase(pos);
            m_spreadQueued.erase(pos);
            m_removeQueued.erase(pos);
            it = m_waterCells.erase(it);
        } else {
            ++it;
        }
    }
}

// ============================================================================
// Legacy API (for compatibility)
// ============================================================================

void WaterSimulation::setWaterLevel(int x, int y, int z, uint8_t level, uint8_t fluidType) {
    glm::ivec3 pos(x, y, z);

    if (level == 0) {
        removeWaterCell(pos, nullptr);
    } else {
        // Convert 0-255 to 0-8
        uint8_t newLevel = (level >= 224) ? LEVEL_SOURCE : ((level / 32) + 1);
        if (newLevel > LEVEL_SOURCE) newLevel = LEVEL_SOURCE;

        WaterCell& cell = m_waterCells[pos];
        cell.level = newLevel;
        cell.fluidType = fluidType;
        cell.isSource = (level == 255);

        if (cell.isSource) {
            m_sourceBlocks.insert(pos);
        }

        queueSpread(pos);
    }
}

void WaterSimulation::addWaterSource(const glm::ivec3& position, uint8_t fluidType) {
    WaterCell& cell = m_waterCells[position];
    cell.level = LEVEL_SOURCE;
    cell.fluidType = fluidType;
    cell.isSource = true;
    m_sourceBlocks.insert(position);
    queueSpread(position);
}

void WaterSimulation::removeWaterSource(const glm::ivec3& position) {
    removeWaterSource(position.x, position.y, position.z, nullptr);
}

bool WaterSimulation::hasWaterSource(const glm::ivec3& position) const {
    return m_sourceBlocks.find(position) != m_sourceBlocks.end();
}

void WaterSimulation::markAsWaterBody(const std::unordered_set<glm::ivec3>& cells, bool infinite) {
    (void)infinite;
    // Mark all cells as sources (infinite water body)
    for (const auto& pos : cells) {
        WaterCell& cell = m_waterCells[pos];
        cell.level = LEVEL_SOURCE;
        cell.isSource = true;
        m_sourceBlocks.insert(pos);
    }
}
