#pragma once

#include <glm/glm.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <queue>
#include <mutex>

class World;
class Chunk;

// Hash function for glm::ivec3 to use in unordered_map
namespace std {
    template <>
    struct hash<glm::ivec3> {
        size_t operator()(const glm::ivec3& v) const {
            size_t h1 = hash<int>()(v.x);
            size_t h2 = hash<int>()(v.y);
            size_t h3 = hash<int>()(v.z);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };
}

/**
 * @brief Minecraft-style BFS water simulation
 *
 * Water mechanics:
 * - Source blocks (level 8) are placed by players or world gen
 * - Water spreads using BFS, decreasing 1 level per block
 * - Water falls infinitely (falling water is always level 8)
 * - Removal uses BFS to instantly clear dependent water
 * - Path-to-drop: water flows preferentially toward edges
 */
class WaterSimulation {
public:
    // Water levels (Minecraft-style)
    static constexpr uint8_t LEVEL_SOURCE = 8;   // Source block (full water)
    static constexpr uint8_t LEVEL_MAX_FLOW = 7; // Maximum flowing level
    static constexpr uint8_t LEVEL_MIN_FLOW = 1; // Minimum flowing level
    static constexpr uint8_t LEVEL_EMPTY = 0;    // No water

    // BFS search radius for path-to-drop
    static constexpr int DROP_SEARCH_RADIUS = 4;

    WaterSimulation();
    ~WaterSimulation();

    // ========== Main Update ==========

    /**
     * @brief Process pending water updates
     * Called each frame to process BFS queues
     */
    void update(float deltaTime, World* world, const glm::vec3& playerPos, float renderDistance);

    // ========== Water Placement/Removal ==========

    /**
     * @brief Place a water source block
     * Triggers BFS spread from this position
     */
    void placeWaterSource(int x, int y, int z, World* world);

    /**
     * @brief Remove a water source block
     * Triggers BFS removal of all dependent water
     */
    void removeWaterSource(int x, int y, int z, World* world);

    /**
     * @brief Trigger water flow from adjacent water when a block is broken
     * This handles natural water (oceans/lakes) by treating water at/below sea level as infinite sources
     * @param lockHeld If true, caller already holds World's chunk mutex - use unsafe methods
     */
    void triggerWaterFlow(int brokenX, int brokenY, int brokenZ, World* world, bool lockHeld = false);

    /**
     * @brief Check if position has a source block
     */
    bool isSource(int x, int y, int z) const;

    /**
     * @brief Check if water at position is "natural" (at or below sea level)
     * Natural water is treated as infinite source
     */
    bool isNaturalWater(int y) const;

    /**
     * @brief Get water level at position (0-8)
     */
    uint8_t getWaterLevel(int x, int y, int z) const;

    // ========== Queries ==========

    uint8_t getFluidType(int x, int y, int z) const;
    glm::vec2 getFlowVector(int x, int y, int z) const;
    uint8_t getShoreCounter(int x, int y, int z) const { return 0; } // Unused in new system

    // ========== Dirty Chunks ==========

    const std::unordered_set<glm::ivec3>& getDirtyChunks() const { return m_dirtyChunks; }
    void clearDirtyChunks() { m_dirtyChunks.clear(); }
    void markChunkDirty(const glm::ivec3& chunkPos) { m_dirtyChunks.insert(chunkPos); }

    // ========== Chunk Lifecycle ==========

    void notifyChunkUnload(int chunkX, int chunkY, int chunkZ);
    void notifyChunkUnloadBatch(const std::vector<std::tuple<int, int, int>>& chunks);

    // ========== Legacy API (for compatibility) ==========

    void setWaterLevel(int x, int y, int z, uint8_t level, uint8_t fluidType = 1);
    void addWaterSource(const glm::ivec3& position, uint8_t fluidType = 1);
    void removeWaterSource(const glm::ivec3& position);
    bool hasWaterSource(const glm::ivec3& position) const;
    void markAsWaterBody(const std::unordered_set<glm::ivec3>& cells, bool infinite = true);
    const std::unordered_set<glm::ivec3>& getActiveWaterChunks() const { return m_activeChunks; }

    // Configuration (mostly unused in new system)
    void setEvaporationEnabled(bool enabled) { (void)enabled; }
    void setFlowSpeed(float speed) { (void)speed; }
    void setLavaFlowMultiplier(float mult) { (void)mult; }

private:
    // ========== Water Data ==========

    struct WaterCell {
        uint8_t level;          // 0-8 (0=empty, 8=source)
        uint8_t fluidType;      // 1=water, 2=lava
        bool isSource;          // True if this is a source block
        glm::vec2 flowDir;      // Flow direction for rendering

        WaterCell() : level(0), fluidType(1), isSource(false), flowDir(0.0f) {}
    };

    std::unordered_map<glm::ivec3, WaterCell> m_waterCells;
    std::unordered_set<glm::ivec3> m_sourceBlocks;  // Fast source lookup

    // ========== BFS Queues ==========

    // Spread queue: positions that need to spread water to neighbors
    std::queue<glm::ivec3> m_spreadQueue;
    std::unordered_set<glm::ivec3> m_spreadQueued;  // Dedup

    // Removal queue: positions to check for removal
    std::queue<glm::ivec3> m_removeQueue;
    std::unordered_set<glm::ivec3> m_removeQueued;  // Dedup

    // ========== Dirty Tracking ==========

    std::unordered_set<glm::ivec3> m_dirtyChunks;
    std::unordered_set<glm::ivec3> m_activeChunks;

    // ========== BFS Methods ==========

    /**
     * @brief BFS spread water from a position
     * Spreads to neighbors at (current_level - 1)
     */
    void bfsSpread(World* world, int maxIterations);

    /**
     * @brief BFS remove water that lost its source
     * Removes all water not connected to a source
     */
    void bfsRemove(World* world, int maxIterations);

    /**
     * @brief Find path to drop using BFS (Minecraft optimization)
     * Returns direction to flow toward a drop, or (0,0) if none found
     */
    glm::ivec2 findPathToDrop(const glm::ivec3& pos, World* world);

    /**
     * @brief Check if water at position can flow (has valid upstream)
     */
    bool hasValidUpstream(const glm::ivec3& pos) const;

    // ========== Helper Methods ==========

    void queueSpread(const glm::ivec3& pos);
    void queueRemove(const glm::ivec3& pos);
    void setWaterCell(const glm::ivec3& pos, uint8_t level, bool isSource, World* world);
    void removeWaterCell(const glm::ivec3& pos, World* world);
    void syncToChunk(const glm::ivec3& pos, uint8_t level, World* world, bool lockHeld = false);
    void markChunkDirtyAt(const glm::ivec3& pos);

    bool isBlockSolid(int x, int y, int z, World* world) const;
    bool canWaterFlowTo(int x, int y, int z, World* world) const;
    glm::ivec3 worldToChunk(const glm::ivec3& worldPos) const;
};
