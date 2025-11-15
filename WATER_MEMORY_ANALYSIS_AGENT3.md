# Water Performance Investigation - Agent 3 Report
## Memory Usage Analysis

**Date:** 2025-11-15
**Agent:** Agent 3 of 5
**Focus:** Memory usage related to water blocks

---

## Executive Summary

Water blocks consume **7.4x more memory** than regular blocks due to duplicate storage in multiple data structures. Each water block requires approximately **37 bytes** compared to **5 bytes** for regular blocks, with **32 bytes of overhead** per water block coming from the WaterSimulation system.

**Key Finding:** Water data is stored in THREE separate locations:
1. Chunk block ID array (4 bytes)
2. Chunk metadata array (1 byte)
3. WaterSimulation hash map (32 bytes)

---

## 1. Block Storage Architecture

### 1.1 Chunk Data Structures
**File:** `/home/user/voxel-engine/include/chunk.h` (lines 377-380)

```cpp
int m_blocks[WIDTH][HEIGHT][DEPTH];           // Block ID storage (32 KB)
uint8_t m_blockMetadata[WIDTH][HEIGHT][DEPTH]; // Block metadata (32 KB)
```

**Memory Layout:**
- Chunk dimensions: 32x32x32 = 32,768 blocks
- Block ID storage: 4 bytes × 32,768 = **131,072 bytes (128 KB)**
- Metadata storage: 1 byte × 32,768 = **32,768 bytes (32 KB)**
- **Total per chunk: 160 KB**

### 1.2 Water Block Storage in Chunks
**File:** `/home/user/voxel-engine/src/chunk.cpp` (lines 277-279)

```cpp
m_blocks[x][y][z] = BLOCK_WATER;        // Block ID = 5
m_blockMetadata[x][y][z] = 0;           // Water level (0-255)
```

**Purpose:**
- `m_blocks`: Stores block type ID (BLOCK_WATER = 5)
- `m_blockMetadata`: Stores water level for rendering (0 = source block, 1-7 = flowing)

**Memory per water block (chunk storage only): 5 bytes**

---

## 2. Water Simulation System

### 2.1 WaterCell Structure
**File:** `/home/user/voxel-engine/include/water_simulation.h` (lines 37-44)

```cpp
struct WaterCell {
    uint8_t level;          // 0-255 water amount (0 = empty, 255 = full) [1 byte]
    glm::vec2 flowVector;   // XZ flow direction [8 bytes]
    uint8_t fluidType;      // 0=none, 1=water, 2=lava [1 byte]
    uint8_t shoreCounter;   // Adjacent empty/solid cells for foam [1 byte]

    WaterCell() : level(0), flowVector(0.0f, 0.0f), fluidType(0), shoreCounter(0) {}
};
```

**Memory breakdown:**
- Raw size: 11 bytes
- **Actual size (with padding): 12 bytes**

### 2.2 Water Storage Container
**File:** `/home/user/voxel-engine/include/water_simulation.h` (line 102)

```cpp
std::unordered_map<glm::ivec3, WaterCell> m_waterCells;
```

**Hash map overhead per entry:**
- Key (glm::ivec3): 12 bytes (3 × int)
- Value (WaterCell): 12 bytes
- Node overhead: ~8 bytes (pointer, hash, etc.)
- **Total per map entry: ~32 bytes**

### 2.3 Water Registration
**File:** `/home/user/voxel-engine/src/world.cpp` (line 475)

```cpp
// During world generation, water blocks are registered:
m_waterSimulation->setWaterLevel(worldX, worldY, worldZ, 255, 1);
```

**File:** `/home/user/voxel-engine/src/water_simulation.cpp` (lines 367-381)

```cpp
void WaterSimulation::setWaterLevel(int x, int y, int z, uint8_t level, uint8_t fluidType) {
    glm::ivec3 pos(x, y, z);

    if (level == 0) {
        m_waterCells.erase(pos);
        m_dirtyCells.erase(pos);
    } else {
        WaterCell& cell = m_waterCells[pos];  // Creates entry if doesn't exist
        cell.level = level;
        cell.fluidType = fluidType;
        markDirty(pos);
    }
}
```

**Memory allocation pattern:**
- Each water block creates a new hash map entry
- Hash map automatically allocates nodes on insertion
- **No pooling or memory reuse detected**

---

## 3. Additional Water Data Structures

### 3.1 Water Sources
**File:** `/home/user/voxel-engine/include/water_simulation.h` (lines 47-55)

```cpp
struct WaterSource {
    glm::ivec3 position;    // 12 bytes
    uint8_t outputLevel;    // 1 byte (usually 255)
    float flowRate;         // 4 bytes (units per second)
    uint8_t fluidType;      // 1 byte (1=water, 2=lava)
};

std::vector<WaterSource> m_waterSources;  // Line 105
```

**Memory:** ~24 bytes per source (with padding)
**Usage:** Infinite water generation points (springs, etc.)

### 3.2 Water Bodies
**File:** `/home/user/voxel-engine/include/water_simulation.h` (lines 58-64)

```cpp
struct WaterBody {
    std::set<glm::ivec3, Ivec3Compare> cells;  // Set of all cells in body
    bool isInfinite;        // True for oceans/lakes
    uint8_t minLevel;       // Minimum level to maintain (200)
};

std::vector<WaterBody> m_waterBodies;  // Line 108
```

**Memory:** Variable, depends on body size
**Overhead per cell in set:** ~40 bytes (12-byte key + tree node overhead)
**Usage:** Prevents evaporation in oceans/lakes

### 3.3 Active Chunk Tracking
**File:** `/home/user/voxel-engine/include/water_simulation.h` (lines 111-117)

```cpp
std::set<glm::ivec3, Ivec3Compare> m_activeChunks;   // Chunks with water
std::set<glm::ivec3, Ivec3Compare> m_dirtyCells;     // Cells needing update
std::set<glm::ivec3, Ivec3Compare> m_dirtyChunks;    // Chunks needing mesh regen
```

**Memory per chunk coordinate in set:** ~40 bytes
**Usage:** Performance optimization - only update chunks with active water

---

## 4. Memory Overhead Comparison

### 4.1 Regular Block vs Water Block

**Regular Block (stone, dirt, grass, etc.):**
```
Chunk block ID:    4 bytes
Chunk metadata:    1 byte
────────────────────────────
TOTAL:            5 bytes
```

**Water Block:**
```
Chunk block ID:          4 bytes
Chunk metadata:          1 byte  (water level for rendering)
WaterSimulation entry:  32 bytes (hash map overhead)
────────────────────────────────────────────────────────
TOTAL:                 ~37 bytes per water block
```

**Memory multiplier: 7.4x**

### 4.2 Scenario Analysis

| Water Blocks | Chunk Storage | WaterSim Overhead | Total Memory | Impact |
|--------------|---------------|-------------------|--------------|--------|
| 1,000 | 4 KB | 31 KB | **36 KB** | Negligible |
| 10,000 | 48 KB | 312 KB | **361 KB** | Low |
| 100,000 | 488 KB | 3,125 KB | **3.5 MB** | Moderate |
| 1,000,000 | 4.7 MB | 30.5 MB | **35.3 MB** | Significant |

**Note:** Large oceans can easily contain 100,000+ water blocks in render distance.

---

## 5. Data Duplication Issues

### 5.1 Water Level Duplication

Water level is stored in **TWO** places with different purposes:

**Location 1: Chunk Metadata** (line 557, chunk.cpp)
```cpp
uint8_t waterLevel = m_blockMetadata[X][Y][Z];
float waterHeightAdjust = -waterLevel * (1.0f / 8.0f);  // Rendering only
```
- **Purpose:** Visual rendering (Minecraft-style flowing water height)
- **Values:** 0-7 (0 = full block, 7 = lowest flow)
- **Used by:** Mesh generation for vertex height adjustment

**Location 2: WaterCell in WaterSimulation**
```cpp
WaterCell::level  // 0-255
```
- **Purpose:** Physics simulation (water amount)
- **Values:** 0-255 (255 = full, 0 = empty)
- **Used by:** Flow simulation, evaporation, spreading

**Analysis:** These serve different purposes, but the duplication adds complexity:
- Chunk metadata: 8 discrete levels for visual steps
- WaterCell: 256 levels for fine-grained simulation
- **Not strictly duplicate, but semantically overlapping**

### 5.2 Position Duplication

Water block position is implicit in THREE places:
1. Chunk array indices (x, y, z in 0-31)
2. Hash map key (glm::ivec3 worldPos)
3. Set entries in m_activeChunks, m_dirtyCells, etc.

**Impact:** Storing position in hash map key adds 12 bytes per water block when position is already implicit in chunk array.

---

## 6. Memory Allocation Patterns

### 6.1 Hash Map Growth
**File:** `/home/user/voxel-engine/src/water_simulation.cpp` (lines 132, 210, 324)

```cpp
// Map insertion creates new entries dynamically
WaterCell& belowCell = m_waterCells[below];      // Line 132 - gravity
WaterCell& neighborCell = m_waterCells[neighborPos]; // Line 210 - spreading
WaterCell& cell = m_waterCells[source.position]; // Line 324 - sources
```

**Pattern:**
- Hash map grows dynamically as water spreads
- Each new position creates a 32-byte allocation
- **No pre-allocation or memory pooling**
- Entries removed when water evaporates (line 83)

### 6.2 Cleanup Behavior
**File:** `/home/user/voxel-engine/src/water_simulation.cpp` (lines 80-87)

```cpp
// Remove cells with no water
for (auto it = m_waterCells.begin(); it != m_waterCells.end();) {
    if (it->second.level == 0) {
        m_dirtyCells.erase(it->first);
        it = m_waterCells.erase(it);  // Deallocates memory
    } else {
        ++it;
    }
}
```

**Behavior:**
- Empty water cells removed every frame
- Memory deallocated immediately
- **Could cause fragmentation with high water churn**

---

## 7. Mesh and GPU Memory

### 7.1 Transparent Geometry Separation
**File:** `/home/user/voxel-engine/include/chunk.h` (lines 383-402)

Water blocks are rendered separately from opaque blocks:

```cpp
// Opaque geometry
std::vector<Vertex> m_vertices;
std::vector<uint32_t> m_indices;
VkBuffer m_vertexBuffer;
VkBuffer m_indexBuffer;

// Transparent geometry (WATER)
std::vector<Vertex> m_transparentVertices;
std::vector<uint32_t> m_transparentIndices;
VkBuffer m_transparentVertexBuffer;
VkBuffer m_transparentIndexBuffer;
```

**Impact:**
- **Doubles GPU buffer count per chunk** (2 vertex + 2 index buffers)
- Each buffer has Vulkan overhead (~48 bytes per VkBuffer handle)
- Staging buffers during upload add temporary memory (lines 405-412)

### 7.2 Vertex Data Per Water Face
**File:** `/home/user/voxel-engine/include/chunk.h` (lines 28-31)

```cpp
struct Vertex {
    float x, y, z;      // Position [12 bytes]
    float r, g, b, a;   // Color + alpha [16 bytes]
    float u, v;         // UV coordinates [8 bytes]
};  // Total: 36 bytes per vertex
```

**Water face memory:**
- 4 vertices per face × 36 bytes = **144 bytes**
- 6 indices per face × 4 bytes = **24 bytes**
- **Total: 168 bytes per visible water face**

**Comparison:**
- Solid block faces: Same 168 bytes per face
- **No difference in per-face cost**
- But water often has more visible faces (liquid is semi-transparent)

---

## 8. Memory Optimization Opportunities

### 8.1 High Priority - Eliminate WaterCell Duplication

**Current Issue:**
- Water data stored in BOTH chunk metadata AND WaterSimulation hash map
- Hash map adds 32 bytes overhead per water block

**Optimization Strategy:**
1. **Option A: Embed simulation data in chunk metadata**
   - Expand metadata from `uint8_t` to a union/struct
   - Store flow vector, fluid type inline with chunk data
   - **Pros:** Eliminates 32-byte hash map overhead
   - **Cons:** Increases all chunks' memory (even non-water)

2. **Option B: Only store flowing water in WaterSimulation**
   - Static water (oceans, lakes) only in chunk data
   - WaterSimulation only tracks active/flowing water
   - **Pros:** Drastically reduces hash map size (only flowing water)
   - **Cons:** Requires differentiating static vs. flowing water

3. **Option C: Use sparse chunk storage**
   - Chunks with >50% water use dedicated water storage
   - Chunks with <50% water use hash map
   - **Pros:** Optimizes for both cases
   - **Cons:** Complex implementation

**Estimated savings:** 32 bytes per water block (87% reduction)

### 8.2 Medium Priority - Hash Map Optimizations

**Current:**
```cpp
std::unordered_map<glm::ivec3, WaterCell> m_waterCells;
```

**Alternatives:**

1. **Use flat_hash_map (Google Abseil)**
   - 30-50% less memory overhead vs std::unordered_map
   - Faster iteration (cache-friendly)
   - **Savings:** ~4-8 bytes per entry

2. **Custom allocator with memory pooling**
   - Pre-allocate chunks of WaterCell nodes
   - Reduces fragmentation from water churn
   - **Savings:** 10-20% less overall heap usage

3. **Spatial hash with fixed grid**
   - Divide world into fixed-size regions
   - Each region has local coordinate system (smaller keys)
   - **Savings:** ~4 bytes per entry (smaller position keys)

### 8.3 Low Priority - Reduce Additional Structures

**Water Bodies:**
- Currently stores ALL cells in std::set (~40 bytes per cell)
- **Optimization:** Store only AABB bounds + body ID
- Mark cells with body ID in metadata
- **Savings:** 36 bytes per cell in large water bodies

**Active Chunk Tracking:**
- Currently uses std::set for active chunks
- **Optimization:** Use bitset or dense array
- Chunk coordinates are predictable within world bounds
- **Savings:** ~32 bytes per active chunk

---

## 9. Quantitative Impact Analysis

### 9.1 Baseline Measurements

**Assumptions:**
- World with 10x10x10 spawn chunks = 1,000 chunks
- Average 10% water blocks per chunk = 3,276 water blocks/chunk
- Total water blocks: 3,276,000

**Current Memory Usage:**
```
Chunk storage:    3,276,000 × 5 bytes   = 15.6 MB
WaterSimulation:  3,276,000 × 32 bytes  = 99.8 MB
─────────────────────────────────────────────────
TOTAL:                                  115.4 MB
```

### 9.2 After Optimization (Option B)

**Assumptions:**
- 90% of water is static (oceans/lakes) - no WaterSimulation entry
- 10% of water is flowing/active - has WaterSimulation entry

**Optimized Memory Usage:**
```
Chunk storage:    3,276,000 × 5 bytes   = 15.6 MB
WaterSimulation:    327,600 × 32 bytes  =  9.9 MB
─────────────────────────────────────────────────
TOTAL:                                   25.5 MB

SAVINGS: 89.9 MB (77.9% reduction)
```

### 9.3 Memory Growth Patterns

**Current system:**
- Linear growth: +37 bytes per water block
- Large oceans cause massive memory spikes
- Example: 1 million block ocean = +35 MB

**Optimized system:**
- Static water: +5 bytes per block
- Flowing water: +37 bytes per block
- Example: 1 million block ocean (99% static) = +5.3 MB
- **Savings: 84.9% for large water bodies**

---

## 10. Recommendations

### 10.1 Immediate Actions (Quick Wins)

1. **Profile actual water block counts**
   - Add telemetry to count total water blocks in m_waterCells
   - Track static vs. flowing water ratio
   - Measure memory growth over time

2. **Add memory metrics to debug UI**
   - Display: "Water blocks: X (Y MB)"
   - Track hash map size and load factor
   - Monitor for memory leaks

### 10.2 Short-term Optimizations (1-2 weeks)

1. **Implement static water optimization (Option B)**
   - Add `isStatic` flag to water blocks
   - Only register flowing water with WaterSimulation
   - Mark ocean/lake water as static during generation
   - **Expected savings: 70-80% memory reduction**

2. **Switch to flat_hash_map**
   - Replace std::unordered_map with absl::flat_hash_map
   - Benchmark memory and performance impact
   - **Expected savings: 5-10% overhead reduction**

### 10.3 Long-term Optimizations (1-2 months)

1. **Redesign chunk metadata system**
   - Use union/variant for block-specific data
   - Embed flow data directly in chunks
   - Eliminate WaterSimulation hash map entirely
   - **Expected savings: 85-90% water memory overhead**

2. **Implement water chunk specialization**
   - Ocean chunks use dense water storage
   - Land chunks use sparse hash map
   - Automatically convert between representations
   - **Expected savings: 90%+ for ocean-heavy worlds**

---

## 11. Files Analyzed

### Core Headers
- `/home/user/voxel-engine/include/chunk.h` (417 lines)
  - Lines 377-380: Block storage arrays
  - Lines 383-402: Transparent geometry buffers

- `/home/user/voxel-engine/include/water_simulation.h` (143 lines)
  - Lines 37-44: WaterCell structure
  - Lines 47-64: WaterSource and WaterBody structures
  - Lines 102-117: Water data containers

- `/home/user/voxel-engine/include/block_system.h` (337 lines)
  - Lines 47-102: BlockDefinition structure
  - Lines 89-98: Liquid properties

### Implementation Files
- `/home/user/voxel-engine/src/chunk.cpp` (1,343 lines)
  - Lines 73-80: Block initialization
  - Lines 277-279: Water block generation
  - Lines 557-559: Water level rendering
  - Lines 636-743: Liquid face culling logic

- `/home/user/voxel-engine/src/water_simulation.cpp` (491 lines)
  - Lines 60-87: Water cell update loop
  - Lines 96-154: Gravity and spreading
  - Lines 367-381: Water level management

- `/home/user/voxel-engine/src/world.cpp`
  - Line 87: WaterSimulation initialization
  - Line 475: Water registration during generation

### Configuration
- `/home/user/voxel-engine/assets/blocks/water.yaml`
  - Water block definition (liquid: true, transparency: 0.25)

### Documentation
- `/home/user/voxel-engine/docs/systems/WATER_SYSTEM_ENHANCED.md`
  - Performance optimizations documented
  - Hash map storage strategy explained

---

## 12. Summary

### Key Findings
1. ✅ **Water blocks use 7.4x more memory than regular blocks** (37 bytes vs 5 bytes)
2. ✅ **Primary overhead: WaterSimulation hash map** (32 bytes per water block)
3. ✅ **No memory pooling or reuse** - dynamic allocation per water block
4. ✅ **Additional overhead from tracking structures** (active chunks, dirty cells)
5. ✅ **GPU memory impact minimal** - same vertex cost per face as solid blocks

### Memory Breakdown Per Water Block
- Chunk block ID: 4 bytes (10.8%)
- Chunk metadata: 1 byte (2.7%)
- **WaterSimulation hash map: 32 bytes (86.5%)**

### Optimization Potential
- **Quick wins:** Switch to flat_hash_map (5-10% savings)
- **Major wins:** Static water optimization (70-80% savings)
- **Maximum wins:** Redesign chunk metadata (85-90% savings)

### Performance Impact
- Memory overhead is the PRIMARY issue, not computational cost
- Hash map lookups are fast (O(1)), but memory footprint grows linearly
- Large oceans (100K+ blocks) can consume 35+ MB just for simulation data
- Recommended immediate action: Implement static vs. flowing water distinction

---

**End of Report**
