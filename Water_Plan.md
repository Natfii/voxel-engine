# Water Flow System Overhaul Plan

## Executive Summary

This document outlines the issues with the current water flow implementation and proposes fixes based on research from Minecraft's water mechanics and cellular automata approaches used in voxel engines.

---

## Current Issues Analysis

### Issue 1: Falling Water Creates Infinite Sources

**Location:** `src/water_simulation.cpp:147-185` (`applyGravity()`)

**Problem:** When water falls, it creates level 255 (full source-equivalent) blocks below:

```cpp
// Current problematic code (line 179):
belowCellPtr->level = 255;  // Falling water creates full water below
```

**Impact:** This creates infinite waterfalls similar to Minecraft, but ALL falling water becomes source-like, making water spread infinitely when it shouldn't.

---

### Issue 2: No Water Retraction When Source Removed

**Location:** `src/water_simulation.cpp:115-145` (`updateWaterCell()`)

**Problem:** Non-source water blocks never check if they're still connected to a source. Once water spreads, it stays forever even if the original source is removed.

```cpp
// Current code - no retraction logic:
void WaterSimulation::updateWaterCell(...) {
    if (hasWaterSource(pos)) {
        cell.level = 255;  // Source maintains level
        spreadHorizontally(pos, cell, world);
        return;
    }
    // Non-source blocks just spread - never retract!
    applyGravity(pos, cell, world);
    spreadHorizontally(pos, cell, world);
}
```

---

### Issue 3: No Pathfinding to Edges/Holes

**Location:** `src/water_simulation.cpp:296-362` (`calculateFlowWeight()`)

**Problem:** The BFS pathfinding was removed for performance, but now water spreads equally in all directions instead of preferring to flow toward edges/drops.

```cpp
// Current simplified code (lines 347-354):
if (!isBlockSolid(below.x, below.y, below.z, world)) {
    weight = 0;  // Best weight - direct downward path
} else {
    weight = 1;  // Neutral weight for horizontal flow - NO PREFERENCE!
}
```

**Expected:** Water should prefer flowing toward holes/edges within 4 blocks (Minecraft behavior).

---

### Issue 4: Spread Level Never Decreases Over Time

**Location:** `src/water_simulation.cpp:187-266` (`spreadHorizontally()`)

**Problem:** Non-source water maintains its level indefinitely. There's no decay/evaporation for flowing water.

```cpp
// Current: spread level is calculated but never decreases
uint8_t spreadLevel = (cell.level >= 32) ? (cell.level - 32) : 0;
// This only affects NEW neighbors, not the current cell
```

---

## Proposed Solutions

### Solution 1: Proper Falling Water (Not Infinite Sources)

**Concept:** Falling water should create flowing blocks (level 224 = "level 7"), not source blocks. Only manually placed water or terrain-generated water should be sources.

**Code Change (`src/water_simulation.cpp:147-185`):**

```cpp
void WaterSimulation::applyGravity(const glm::ivec3& pos, WaterCell& cell, World* world) {
    glm::ivec3 below = pos - glm::ivec3(0, 1, 0);

    if (isBlockSolid(below.x, below.y, below.z, world)) {
        return;
    }

    auto it = m_waterCells.find(below);
    WaterCell* belowCellPtr = nullptr;
    if (it != m_waterCells.end()) {
        belowCellPtr = &it->second;
        if (belowCellPtr->level > 0 && belowCellPtr->fluidType != cell.fluidType) {
            return;
        }
    }

    // FIXED: Falling water creates "flowing" block (level 224), not source
    // Only actual sources (registered via addWaterSource) should be 255
    uint8_t fallingLevel = hasWaterSource(pos) ? 255 : 224;  // Level 7 flowing

    if (!belowCellPtr || belowCellPtr->level < fallingLevel) {
        if (!belowCellPtr) {
            belowCellPtr = &m_waterCells[below];
            belowCellPtr->fluidType = cell.fluidType;
        }

        belowCellPtr->level = fallingLevel;
        belowCellPtr->flowVector = glm::vec2(0.0f, -1.0f);

        markDirty(below);
        syncWaterLevelToChunk(below, fallingLevel, world);
    }
}
```

---

### Solution 2: Water Retraction System

**Concept:** Flowing water should check each update if it's still receiving water from a higher-level neighbor. If not, it should decrease by 32 per update until empty.

**New Function to Add (`src/water_simulation.cpp`):**

```cpp
bool WaterSimulation::hasUpstreamWater(const glm::ivec3& pos, World* world) {
    // Check if this position is a source
    if (hasWaterSource(pos)) return true;

    // Check above (water falling from above)
    glm::ivec3 above = pos + glm::ivec3(0, 1, 0);
    auto aboveIt = m_waterCells.find(above);
    if (aboveIt != m_waterCells.end() && aboveIt->second.level > 0) {
        return true;
    }

    // Check horizontal neighbors for higher water level
    uint8_t currentLevel = getWaterLevel(pos.x, pos.y, pos.z);
    glm::ivec3 neighbors[4] = {
        pos + glm::ivec3(1, 0, 0),
        pos + glm::ivec3(-1, 0, 0),
        pos + glm::ivec3(0, 0, 1),
        pos + glm::ivec3(0, 0, -1)
    };

    for (const auto& neighbor : neighbors) {
        auto it = m_waterCells.find(neighbor);
        if (it != m_waterCells.end()) {
            // Neighbor has more water than us (would flow to us)
            if (it->second.level > currentLevel + 32) {
                return true;
            }
            // Neighbor is a source
            if (hasWaterSource(neighbor)) {
                return true;
            }
        }
    }

    return false;
}
```

**Modified `updateWaterCell()` (`src/water_simulation.cpp:115-145`):**

```cpp
void WaterSimulation::updateWaterCell(const glm::ivec3& pos, WaterCell& cell, World* world, float deltaTime) {
    if (cell.level == 0) return;

    // Source blocks always maintain level and spread
    if (hasWaterSource(pos)) {
        cell.level = 255;
        spreadHorizontally(pos, cell, world);
        return;
    }

    // NEW: Retraction check for non-source water
    if (!hasUpstreamWater(pos, world)) {
        // No upstream water - decrease level (retract)
        if (cell.level > 32) {
            cell.level -= 32;
            markDirty(pos);
            syncWaterLevelToChunk(pos, cell.level, world);

            // Mark neighbors dirty so they can also retract
            markNeighborsDirty(pos);
        } else {
            cell.level = 0;  // Will be removed in cleanup
        }
        return;
    }

    // Normal flow: gravity then spread
    applyGravity(pos, cell, world);
    if (cell.level > 0) {
        spreadHorizontally(pos, cell, world);
    }
    syncWaterLevelToChunk(pos, cell.level, world);
}

void WaterSimulation::markNeighborsDirty(const glm::ivec3& pos) {
    glm::ivec3 neighbors[6] = {
        pos + glm::ivec3(1, 0, 0),
        pos + glm::ivec3(-1, 0, 0),
        pos + glm::ivec3(0, 1, 0),
        pos + glm::ivec3(0, -1, 0),
        pos + glm::ivec3(0, 0, 1),
        pos + glm::ivec3(0, 0, -1)
    };
    for (const auto& neighbor : neighbors) {
        if (m_waterCells.find(neighbor) != m_waterCells.end()) {
            markDirty(neighbor);
        }
    }
}
```

---

### Solution 3: Simple Edge-Finding (Minecraft-Style BFS)

**Concept:** Before spreading horizontally, find the best direction to flow by checking which directions lead to a drop within 4 blocks.

**New Helper Function (`src/water_simulation.cpp`):**

```cpp
// Lightweight BFS to find nearest edge within 4 blocks
// Returns bitmask of preferred directions (bit 0=+X, 1=-X, 2=+Z, 3=-Z)
uint8_t WaterSimulation::findPreferredFlowDirections(const glm::ivec3& pos, World* world) {
    uint8_t preferredDirs = 0;
    int bestDistance = 5;  // Max search distance + 1

    glm::ivec3 directions[4] = {
        glm::ivec3(1, 0, 0),   // +X (bit 0)
        glm::ivec3(-1, 0, 0),  // -X (bit 1)
        glm::ivec3(0, 0, 1),   // +Z (bit 2)
        glm::ivec3(0, 0, -1)   // -Z (bit 3)
    };

    for (int dir = 0; dir < 4; dir++) {
        // Check if neighbor is passable
        glm::ivec3 neighbor = pos + directions[dir];
        if (isBlockSolid(neighbor.x, neighbor.y, neighbor.z, world)) {
            continue;
        }

        // BFS from this neighbor to find nearest drop
        int distance = findDistanceToEdge(neighbor, world, 4);

        if (distance < bestDistance) {
            bestDistance = distance;
            preferredDirs = (1u << dir);  // Reset to just this direction
        } else if (distance == bestDistance && distance < 5) {
            preferredDirs |= (1u << dir);  // Add this direction
        }
    }

    return preferredDirs;  // 0 means no preference (spread equally)
}

int WaterSimulation::findDistanceToEdge(const glm::ivec3& start, World* world, int maxDist) {
    // Simple BFS to find nearest drop
    std::queue<std::pair<glm::ivec3, int>> queue;
    std::unordered_set<glm::ivec3> visited;

    queue.push({start, 0});
    visited.insert(start);

    while (!queue.empty()) {
        auto [pos, dist] = queue.front();
        queue.pop();

        // Check if block below is air (found edge!)
        glm::ivec3 below = pos - glm::ivec3(0, 1, 0);
        if (!isBlockSolid(below.x, below.y, below.z, world) &&
            m_waterCells.find(below) == m_waterCells.end()) {
            return dist;
        }

        if (dist >= maxDist) continue;

        // Expand to horizontal neighbors
        glm::ivec3 dirs[4] = {
            glm::ivec3(1, 0, 0), glm::ivec3(-1, 0, 0),
            glm::ivec3(0, 0, 1), glm::ivec3(0, 0, -1)
        };

        for (const auto& d : dirs) {
            glm::ivec3 next = pos + d;
            if (visited.find(next) != visited.end()) continue;
            if (isBlockSolid(next.x, next.y, next.z, world)) continue;

            visited.insert(next);
            queue.push({next, dist + 1});
        }
    }

    return 1000;  // No edge found
}
```

**Modified `spreadHorizontally()` to use preferred directions:**

```cpp
void WaterSimulation::spreadHorizontally(const glm::ivec3& pos, WaterCell& cell, World* world) {
    if (cell.level < 64) return;

    uint8_t spreadLevel = (cell.level >= 32) ? (cell.level - 32) : 0;
    if (spreadLevel < 32) return;

    // NEW: Find preferred flow directions (toward edges)
    uint8_t preferredDirs = findPreferredFlowDirections(pos, world);

    glm::ivec3 neighbors[4] = {
        pos + glm::ivec3(1, 0, 0),
        pos + glm::ivec3(-1, 0, 0),
        pos + glm::ivec3(0, 0, 1),
        pos + glm::ivec3(0, 0, -1)
    };

    glm::vec2 directions[4] = {
        glm::vec2(1.0f, 0.0f),
        glm::vec2(-1.0f, 0.0f),
        glm::vec2(0.0f, 1.0f),
        glm::vec2(0.0f, -1.0f)
    };

    for (int i = 0; i < 4; i++) {
        // If we have preferred directions, only spread in those
        if (preferredDirs != 0 && !(preferredDirs & (1u << i))) {
            continue;
        }

        glm::ivec3 neighborPos = neighbors[i];

        if (isBlockSolid(neighborPos.x, neighborPos.y, neighborPos.z, world)) {
            continue;
        }

        auto it = m_waterCells.find(neighborPos);
        WaterCell* neighborCellPtr = nullptr;
        uint8_t neighborLevel = 0;

        if (it != m_waterCells.end()) {
            neighborCellPtr = &it->second;
            neighborLevel = neighborCellPtr->level;

            if (neighborLevel > 0 && neighborCellPtr->fluidType != cell.fluidType) {
                continue;
            }

            if (neighborLevel >= spreadLevel) {
                continue;
            }
        }

        if (!neighborCellPtr) {
            neighborCellPtr = &m_waterCells[neighborPos];
            neighborCellPtr->fluidType = cell.fluidType;
        }

        neighborCellPtr->level = spreadLevel;
        neighborCellPtr->flowVector = directions[i];

        markDirty(neighborPos);
        syncWaterLevelToChunk(neighborPos, spreadLevel, world);
    }
}
```

---

## Header File Changes

**Add to `include/water_simulation.h` (around line 163):**

```cpp
// New helper methods for water flow
bool hasUpstreamWater(const glm::ivec3& pos, World* world);
void markNeighborsDirty(const glm::ivec3& pos);
uint8_t findPreferredFlowDirections(const glm::ivec3& pos, World* world);
int findDistanceToEdge(const glm::ivec3& start, World* world, int maxDist);
```

---

## Alternative: Cellular Automata Approach

For more realistic (non-Minecraft) water, consider the cellular automata approach:

### Key Differences:
| Feature | Current (Minecraft-style) | Cellular Automata |
|---------|---------------------------|-------------------|
| Water Amount | Discrete levels (0-7) | Continuous mass (0.0-1.0+) |
| Compression | None | Water can compress slightly |
| Pressure | None | Pressure drives upward flow |
| Settling | Instant | Gradual with damping |

### Cellular Automata Rules (from [W-Shadow](https://w-shadow.com/blog/2009/09/01/simple-fluid-simulation/)):

```cpp
// Per-cell update:
// 1. Flow DOWN (with compression)
float stableBelow = getStableState(cell.mass + below.mass);
float flowDown = min(cell.mass, stableBelow - below.mass);
transferDown(flowDown);

// 2. Flow LEFT (equalize)
float flowLeft = (cell.mass - left.mass) / 2.0f;
transferLeft(flowLeft);

// 3. Flow RIGHT (equalize)
float flowRight = (cell.mass - right.mass) / 2.0f;
transferRight(flowRight);

// 4. Flow UP (pressure overflow)
float flowUp = cell.mass - stableHere;
if (flowUp > 0) transferUp(flowUp);
```

---

## Performance Considerations

### Current Optimizations to Keep:
1. **Dirty cell tracking** (`m_dirtyCells`) - Only update changed cells
2. **Chunk freezing** - Only simulate near player
3. **Flow weight cache** - Avoid redundant calculations

### New BFS Concerns:
The proposed `findDistanceToEdge()` BFS could be expensive. Mitigations:
1. **Cache results** for 1 frame (positions don't change that fast)
2. **Limit search** to 4 blocks (Minecraft default)
3. **Skip for full-level water** (sources always spread equally)

---

## Testing Plan

1. **Place water bucket** - Should spread 7 blocks, then stop
2. **Remove source** - Water should retract toward source location and disappear
3. **Water near cliff** - Should flow preferentially toward the drop
4. **Waterfall** - Should create flowing (not source) blocks below
5. **Two sources merge** - Should not create infinite water loops

---

## References

- [Minecraft Wiki - Fluid](https://minecraft.wiki/w/Fluid) - Official water mechanics
- [W-Shadow - Simple Fluid Simulation](https://w-shadow.com/blog/2009/09/01/simple-fluid-simulation/) - Cellular automata approach
- [jgallant - 2D Liquid Simulator](https://www.jgallant.com/2d-liquid-simulator-with-cellular-automaton-in-unity/) - Unity implementation
- [Let's Make a Voxel Engine - Water](https://sites.google.com/site/letsmakeavoxelengine/home/water) - Voxel engine tutorial
- [voxel/ideas - Water](https://github.com/voxel/ideas/issues/1) - Community implementations

---

## Implementation Priority

1. **HIGH** - Fix falling water creating sources (Solution 1) - Quick fix, big impact
2. **HIGH** - Add water retraction (Solution 2) - Required for realistic behavior
3. **MEDIUM** - Add edge-finding BFS (Solution 3) - Nice to have, more complex
4. **LOW** - Cellular automata rewrite - Major overhaul, optional

---

## Files to Modify

| File | Changes |
|------|---------|
| `src/water_simulation.cpp` | Main logic changes (lines 115-266, 296-362) |
| `include/water_simulation.h` | Add new method declarations (line ~163) |

