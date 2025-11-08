#pragma once

#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include <set>
#include <queue>

class World;
class Chunk;

// Custom comparator for glm::ivec3 to use in std::set
struct Ivec3Compare {
    bool operator()(const glm::ivec3& a, const glm::ivec3& b) const {
        if (a.x != b.x) return a.x < b.x;
        if (a.y != b.y) return a.y < b.y;
        return a.z < b.z;
    }
};

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

class WaterSimulation {
public:
    // Water cell data stored per voxel
    struct WaterCell {
        uint8_t level;          // 0-255 water amount (0 = empty, 255 = full)
        glm::vec2 flowVector;   // XZ flow direction
        uint8_t fluidType;      // 0=none, 1=water, 2=lava
        uint8_t shoreCounter;   // Adjacent empty/solid cells for foam effect

        WaterCell() : level(0), flowVector(0.0f, 0.0f), fluidType(0), shoreCounter(0) {}
    };

    // Water source that continuously generates water
    struct WaterSource {
        glm::ivec3 position;
        uint8_t outputLevel;    // Water level to maintain (usually 255)
        float flowRate;         // Units added per second
        uint8_t fluidType;      // 1=water, 2=lava

        WaterSource(const glm::ivec3& pos, uint8_t type = 1)
            : position(pos), outputLevel(255), flowRate(128.0f), fluidType(type) {}
    };

    // Large body of water (ocean/lake)
    struct WaterBody {
        std::set<glm::ivec3, Ivec3Compare> cells;
        bool isInfinite;        // True for oceans/lakes (don't evaporate)
        uint8_t minLevel;       // Minimum water level to maintain

        WaterBody() : isInfinite(true), minLevel(200) {}
    };

    WaterSimulation();
    ~WaterSimulation();

    // Main update loop
    void update(float deltaTime, World* world);

    // Water manipulation
    void setWaterLevel(int x, int y, int z, uint8_t level, uint8_t fluidType = 1);
    uint8_t getWaterLevel(int x, int y, int z) const;
    uint8_t getFluidType(int x, int y, int z) const;
    glm::vec2 getFlowVector(int x, int y, int z) const;
    uint8_t getShoreCounter(int x, int y, int z) const;

    // Water sources
    void addWaterSource(const glm::ivec3& position, uint8_t fluidType = 1);
    void removeWaterSource(const glm::ivec3& position);
    bool hasWaterSource(const glm::ivec3& position) const;

    // Water bodies
    void markAsWaterBody(const std::set<glm::ivec3, Ivec3Compare>& cells, bool infinite = true);

    // Query active chunks
    const std::set<glm::ivec3, Ivec3Compare>& getActiveWaterChunks() const { return m_activeChunks; }

    // Configuration
    void setEvaporationEnabled(bool enabled) { m_enableEvaporation = enabled; }
    void setFlowSpeed(float speed) { m_flowSpeed = speed; }
    void setLavaFlowMultiplier(float mult) { m_lavaFlowMultiplier = mult; }

private:
    // Per-voxel water data
    std::unordered_map<glm::ivec3, WaterCell> m_waterCells;

    // Water sources
    std::vector<WaterSource> m_waterSources;

    // Water bodies
    std::vector<WaterBody> m_waterBodies;

    // Chunks with active water (for optimization)
    std::set<glm::ivec3, Ivec3Compare> m_activeChunks;

    // Configuration
    bool m_enableEvaporation;
    float m_flowSpeed;
    float m_lavaFlowMultiplier;
    uint8_t m_evaporationThreshold;

    // Frame counter for spreading updates
    int m_frameOffset;

    // Internal simulation methods
    void updateWaterCell(const glm::ivec3& pos, WaterCell& cell, World* world, float deltaTime);
    void applyGravity(const glm::ivec3& pos, WaterCell& cell, World* world);
    void spreadHorizontally(const glm::ivec3& pos, WaterCell& cell, World* world);
    int calculateFlowWeight(const glm::ivec3& from, const glm::ivec3& to, World* world);
    void updateShoreCounter(const glm::ivec3& pos, WaterCell& cell, World* world);
    void updateWaterSources(float deltaTime);
    void updateWaterBodies();
    void updateActiveChunks();

    // Helper methods
    bool isBlockSolid(int x, int y, int z, World* world) const;
    bool isBlockLiquid(int x, int y, int z, World* world) const;
    glm::ivec3 worldToChunk(const glm::ivec3& worldPos) const;
};
