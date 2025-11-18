# VOXEL LIGHTING SYSTEM - INDUSTRY STANDARD IMPLEMENTATION PLAN

**Inspired by**: Minecraft & Classicube
**Researched**: 2025-01-17
**Status**: PLANNING PHASE - DO NOT IMPLEMENT YET

---

## TABLE OF CONTENTS

1. [Executive Summary](#executive-summary)
2. [Lighting System Overview](#lighting-system-overview)
3. [Data Structures](#data-structures)
4. [Algorithm Specifications](#algorithm-specifications)
5. [Implementation Architecture](#implementation-architecture)
6. [Integration with Existing Engine](#integration-with-existing-engine)
7. [Code Structure & Files](#code-structure--files)
8. [Optimization Techniques](#optimization-techniques)
9. [Testing & Validation](#testing--validation)
10. [Implementation Phases](#implementation-phases)
11. [Sources & References](#sources--references)

---

## EXECUTIVE SUMMARY

This document outlines a complete lighting system implementation for the voxel engine based on industry-standard techniques from Minecraft, Seeds of Andromeda, and research from 0fps.net.

**Core Approach**: Flood-fill BFS light propagation with two separate channels (sunlight + torch light), smooth vertex lighting with ambient occlusion, and incremental queue-based updates for real-time performance.

**Performance Target**: Sub-millisecond lighting updates, processed over multiple frames to maintain 60 FPS during heavy light changes.

**Estimated Timeline**: 8 weeks

---

## LIGHTING SYSTEM OVERVIEW

### Lighting Channels

The system uses **TWO separate 4-bit lighting channels**:

#### Channel 1: Sky Light (Sunlight)
- **Range**: 0-15 (4 bits)
- **Source**: Sun (top of world)
- **Propagation**: Downward without attenuation, horizontal with -1 decay per block
- **Blocking**: Stopped by opaque blocks

#### Channel 2: Block Light (Torches/Emissive)
- **Range**: 0-15 (4 bits)
- **Sources**: Torches (14), Lava (15), Glowstone (15)
- **Propagation**: All directions with -1 decay per block
- **Maximum range**: 15 blocks from source

**Total Storage**: 1 byte per voxel (4 bits sky + 4 bits block)
**Per-Chunk Storage**: 32×32×32 = 32,768 blocks × 1 byte = **32 KB**

### Light Levels

| Level | Brightness | Example Sources |
|-------|------------|-----------------|
| 15 | 100% | Sun, torch at source, lava |
| 14 | 93% | 1 block from torch |
| 13 | 87% | 2 blocks from torch |
| 12 | 80% | 3 blocks from torch |
| 11 | 73% | 4 blocks from torch |
| 10 | 67% | 5 blocks from torch |
| 9 | 60% | 6 blocks from torch |
| 8 | 53% | 7 blocks from torch |
| 7 | 47% | Edges of torch radius |
| 6 | 40% | Deep cave ambience |
| 5 | 33% | Very dim |
| 4 | 27% | Nearly dark |
| 3 | 20% | Extremely dark |
| 2 | 13% | Almost pitch black |
| 1 | 7% | Barely visible |
| 0 | 0% | Complete darkness |

### Smooth Lighting

Instead of per-block lighting, use **per-vertex lighting** for smooth gradients:
- Each vertex samples adjacent blocks
- Averages light values from up to 8 neighbors
- Creates smooth transitions between lit/unlit areas
- Matches modern Minecraft "Smooth Lighting" option

---

## DATA STRUCTURES

### Per-Block Storage (Recommended)

```cpp
// NEW FILE: include/block_light.h
struct BlockLight {
    uint8_t skyLight  : 4;  // 0-15 sunlight
    uint8_t blockLight : 4;  // 0-15 torch light
};

static_assert(sizeof(BlockLight) == 1, "BlockLight must be 1 byte");
```

### Per-Block Storage (Future Enhancement - Colored Lighting)

```cpp
struct BlockLightRGB {
    uint8_t skyR   : 4;
    uint8_t skyG   : 4;
    uint8_t skyB   : 4;
    uint8_t blockR : 4;
    uint8_t blockG : 4;
    uint8_t blockB : 4;
    uint8_t unused : 8;
};

// Per chunk: 32,768 blocks × 2 bytes = 64 KB
```

### Chunk Light Storage

```cpp
// MODIFY: include/chunk.h
class Chunk {
public:
    // NEW: Light data accessors
    uint8_t getSkyLight(int x, int y, int z) const;
    uint8_t getBlockLight(int x, int y, int z) const;
    void setSkyLight(int x, int y, int z, uint8_t value);
    void setBlockLight(int x, int y, int z, uint8_t value);

    // NEW: Dirty flag
    void markLightingDirty() { m_lightingDirty = true; }
    bool isLightingDirty() const { return m_lightingDirty; }

private:
    // NEW: Light storage (32 KB per chunk)
    std::array<BlockLight, WIDTH * HEIGHT * DEPTH> m_lightData;
    bool m_lightingDirty = false;
};
```

### Global Lighting System

```cpp
// NEW FILE: include/lighting_system.h
class LightingSystem {
public:
    LightingSystem(World* world);

    // Initialize lighting for entire world
    void initializeWorldLighting();

    // Update lighting incrementally (call every frame)
    void update(float deltaTime);

    // Add/remove light sources
    void addLightSource(const glm::vec3& pos, uint8_t lightLevel);
    void removeLightSource(const glm::vec3& pos);

    // Recalculate when block changes
    void onBlockChanged(const glm::ivec3& pos, bool wasOpaque, bool isOpaque);

private:
    World* m_world;

    struct LightNode {
        glm::ivec3 position;
        uint8_t lightLevel;
        bool isSkyLight;
    };

    std::deque<LightNode> m_lightAddQueue;
    std::deque<LightNode> m_lightRemoveQueue;
    std::unordered_set<Chunk*> m_dirtyChunks;

    static constexpr int MAX_LIGHT_UPDATES_PER_FRAME = 500;
};
```

---

## ALGORITHM SPECIFICATIONS

### Sunlight Propagation (BFS Flood Fill)

#### Initial World Generation

```
ALGORITHM: GenerateSunlight(World world)
-----------------------------------------
FOR each column (x, z) in world:
    1. Find highest non-air block Y
    2. Set skyLight = 15 for all air above highest solid
    3. Propagate downward:
       - If transparent: maintain skyLight = 15
       - If opaque: stop, set skyLight = 0 below
    4. Queue horizontal spread from lit blocks

FOR each queued block:
    Run BFS_PropagateLight(block, SKY_LIGHT)
```

#### BFS Propagation

```
ALGORITHM: BFS_PropagateLight(startBlock, channel)
---------------------------------------------------
INPUT:  startBlock with lightLevel > 0
OUTPUT: All neighbors updated with correct values

1. Queue Q = {startBlock}
2. WHILE Q not empty:
    a. node = Q.dequeue()
    b. currentLight = node.lightLevel
    c. FOR each of 6 neighbors:
        i.   Calculate newLight:
             - Downward (skylight only): currentLight (no decay)
             - Horizontal/Up: currentLight - 1
        ii.  IF neighbor opaque: skip
        iii. IF neighbor.lightLevel < newLight:
             - Set neighbor.lightLevel = newLight
             - Q.enqueue(neighbor)
             - Mark chunk dirty
```

**Key Properties**:
- Sunlight travels straight down without decay
- Sunlight spreads horizontally with -1 decay per block
- Maximum horizontal range: 15 blocks from sky column

### Torch Light Propagation

#### Torch Placement

```
ALGORITHM: PlaceTorch(position, emissionLevel)
-----------------------------------------------
1. Set blockLight[position] = emissionLevel (14 for torch)
2. Queue position for BFS
3. Run BFS_PropagateTorchLight(position)
```

#### BFS Propagation

```
ALGORITHM: BFS_PropagateTorchLight(startBlock)
-----------------------------------------------
INPUT:  startBlock with blockLight > 0
OUTPUT: All neighbors within 15 blocks lit

1. Queue Q = {startBlock}
2. WHILE Q not empty:
    a. node = Q.dequeue()
    b. currentLight = node.blockLight
    c. IF currentLight <= 1: skip
    d. FOR each of 6 neighbors:
        i.   newLight = currentLight - 1
        ii.  IF neighbor opaque: skip
        iii. IF neighbor.blockLight < newLight:
             - Set neighbor.blockLight = newLight
             - Q.enqueue(neighbor)
             - Mark chunk dirty
```

**Key Properties**:
- Decays by 1 in ALL directions (spherical propagation)
- Maximum range: 15 blocks
- Light level 14 torch → 1 light at 13 blocks distance

### Light Removal Algorithm (Two-Queue BFS)

**The Challenge**: Removing a torch leaves "ghost lighting" from overlapping sources. Simple recursive removal fails.

#### Solution

```
ALGORITHM: RemoveLight(position, channel)
------------------------------------------
INPUT:  position of removed light source
OUTPUT: Correct lighting everywhere

PHASE 1: Collect Affected Blocks
---------------------------------
1. removalQueue = {position}
2. addQueue = empty
3. WHILE removalQueue not empty:
    a. node = removalQueue.dequeue()
    b. oldLight = node.lightValue
    c. Set node.lightValue = 0
    d. FOR each neighbor:
        i.   neighborLight = neighbor.lightValue
        ii.  IF neighborLight < oldLight AND neighborLight > 0:
             - removalQueue.enqueue(neighbor)  // Propagate removal
        iii. ELSE IF neighborLight >= oldLight:
             - addQueue.enqueue(neighbor)  // Potential light source

PHASE 2: Re-propagate Valid Light
----------------------------------
4. FOR each block in addQueue:
    a. IF block has light source:
       - Run BFS_PropagateLight(block)
    b. ELSE:
       - Recalculate from neighbors
       - IF any neighbor has higher light:
          * Set this = max(neighbor) - 1
          * addQueue.enqueue(this)
```

#### Visual Example

```
Before Removal:     After Phase 1:    After Phase 2:
T1 = torch (14)     (cleared area)    (restored)

  13 14 13            0  0  0           13 14 13
  13 T1 13            0  0  0           13 T1 13
  13 14 13            0 T2  0           13 14 13
     T2 ←removed         ↑                 ↑
                      Found T1!         Restored!
```

### Smooth Lighting & Ambient Occlusion

#### Per-Vertex Calculation

```
ALGORITHM: CalculateVertexLight(vertexPos, faceNormal)
-------------------------------------------------------
INPUT:  Vertex position on block face
OUTPUT: Smooth light value (0.0 - 1.0)

1. Sample 4 adjacent blocks around vertex:
   - side1 (along edge 1)
   - side2 (along edge 2)
   - corner (diagonal)
   - center (current block)

2. Calculate Ambient Occlusion:
   IF side1 AND side2 both opaque:
       AO = 0  (darkest)
   ELSE:
       AO = 3 - (side1_opaque + side2_opaque + corner_opaque)

3. Average light from 4 samples:
   avgLight = (side1.light + side2.light + corner.light + center.light) / 4.0

4. Apply AO:
   finalLight = avgLight * (AO / 3.0)

5. Return finalLight (0.0 - 1.0 range)
```

#### Quad Orientation Fix

Prevents interpolation artifacts across quad diagonals:

```cpp
// Compare diagonal AO values to choose triangulation
if (vertex[0][0].AO + vertex[1][1].AO > vertex[0][1].AO + vertex[1][0].AO) {
    // Flip quad diagonal
    generateFlippedQuad();
} else {
    generateNormalQuad();
}
```

---

## IMPLEMENTATION ARCHITECTURE

### New Files

```
voxel-engine/
├── include/
│   ├── lighting_system.h        [NEW] Core lighting system
│   └── block_light.h            [NEW] Light data structures
├── src/
│   └── lighting_system.cpp      [NEW] Implementation
├── shaders/
│   ├── chunk.vert               [MODIFY] Add light attributes
│   └── chunk.frag               [MODIFY] Apply lighting
└── tests/
    └── test_lighting.cpp        [NEW] Unit tests
```

### Modified Files

```
include/chunk.h          - Add light storage, accessors
src/chunk.cpp            - Implement light methods, smooth lighting in mesh
include/world.h          - Add LightingSystem* member
src/world.cpp            - Integrate lighting with block changes
src/main.cpp             - Initialize lighting system
include/block_registry.h - Add isEmissive, emissionLevel properties
include/vertex.h         - Add lightLevel, ambientOcclusion fields
```

---

## INTEGRATION WITH EXISTING ENGINE

### Initialization

```cpp
// MODIFY: src/main.cpp
int main() {
    // ... existing initialization ...

    World world(...);
    world.generateSpawnChunks(...);

    // NEW: Initialize lighting system
    LightingSystem lighting(&world);
    lighting.initializeWorldLighting();  // Generate sunlight

    world.decorateWorld();
    world.createBuffers(&renderer);

    // Main loop
    while (!glfwWindowShouldClose(window)) {
        // ... input ...

        // NEW: Update lighting incrementally
        lighting.update(deltaTime);

        // ... rendering ...
    }
}
```

### Block Placement/Breaking

```cpp
// MODIFY: src/world.cpp - breakBlock()
void World::breakBlock(float worldX, float worldY, float worldZ, VulkanRenderer* renderer) {
    int oldBlockID = getBlockAt(worldX, worldY, worldZ);
    bool wasOpaque = registry.get(oldBlockID).isOpaque;

    chunk->setBlock(blockCoords.localX, blockCoords.localY, blockCoords.localZ, BLOCK_AIR);

    // NEW: Update lighting
    glm::ivec3 blockPos(worldX, worldY, worldZ);
    m_lightingSystem->onBlockChanged(blockPos, wasOpaque, false);

    chunk->generateMesh(this, true);
}

// MODIFY: src/world.cpp - placeBlock()
void World::placeBlock(const glm::vec3& position, int blockID, VulkanRenderer* renderer) {
    bool isOpaque = registry.get(blockID).isOpaque;
    chunk->setBlock(blockCoords.localX, blockCoords.localY, blockCoords.localZ, blockID);

    // NEW: Update lighting
    glm::ivec3 blockPos(position);
    m_lightingSystem->onBlockChanged(blockPos, false, isOpaque);

    // NEW: If emissive (torch), add light source
    if (registry.get(blockID).isEmissive) {
        m_lightingSystem->addLightSource(position, registry.get(blockID).emissionLevel);
    }

    chunk->generateMesh(this, true);
}
```

### Mesh Generation

```cpp
// MODIFY: src/chunk.cpp - generateMesh()
void Chunk::generateMesh(World* world, bool callerHoldsLock) {
    // ... existing greedy meshing ...

    // For each quad vertex:
    for (int i = 0; i < 4; i++) {
        Vertex vertex;
        // ... position, normal, texCoord ...

        // NEW: Calculate smooth lighting
        vertex.lightLevel = calculateVertexLight(vertexPos, faceNormal);
        vertex.ambientOcclusion = calculateAO(vertexPos, faceNormal);

        vertices.push_back(vertex);
    }
}

float Chunk::calculateVertexLight(const glm::vec3& pos, const glm::vec3& normal) {
    // Sample 4 adjacent blocks
    glm::ivec3 offsets[4] = { /* calculate based on normal */ };

    float totalLight = 0.0f;
    for (int i = 0; i < 4; i++) {
        glm::ivec3 samplePos = pos + offsets[i];
        uint8_t skyLight = getSkyLightAt(samplePos);
        uint8_t blockLight = getBlockLightAt(samplePos);

        // Combine: max of sky and block light
        uint8_t combinedLight = std::max(skyLight, blockLight);
        totalLight += combinedLight / 15.0f;  // Normalize to 0-1
    }

    return totalLight / 4.0f;  // Average
}
```

### Shader Integration

```glsl
// MODIFY: shaders/chunk.vert
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inColor;
layout(location = 4) in float inLightLevel;      // NEW
layout(location = 5) in float inAmbientOcclusion; // NEW

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out float fragLightLevel;     // NEW
layout(location = 3) out float fragAO;             // NEW

void main() {
    gl_Position = ubo.projection * ubo.view * ubo.model * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragTexCoord = inTexCoord;
    fragLightLevel = inLightLevel;
    fragAO = inAmbientOcclusion;
}
```

```glsl
// MODIFY: shaders/chunk.frag
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in float fragLightLevel;     // NEW
layout(location = 3) in float fragAO;             // NEW

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texColor = texture(texSampler, fragTexCoord);

    // NEW: Apply smooth lighting
    float lightIntensity = fragLightLevel * fragAO;
    vec3 finalColor = texColor.rgb * fragColor * lightIntensity;

    outColor = vec4(finalColor, texColor.a);
}
```

---

## OPTIMIZATION TECHNIQUES

### Incremental Updates

**Problem**: Full lighting recalculation would freeze the game

**Solution**: Process updates over multiple frames

```cpp
void LightingSystem::update(float deltaTime) {
    int updatesThisFrame = 0;
    const int MAX_UPDATES = 500;

    // Process light additions
    while (!m_lightAddQueue.empty() && updatesThisFrame < MAX_UPDATES) {
        LightNode node = m_lightAddQueue.front();
        m_lightAddQueue.pop_front();

        propagateLightStep(node);
        updatesThisFrame++;
    }

    // Process light removals (higher priority)
    updatesThisFrame = 0;
    while (!m_lightRemoveQueue.empty() && updatesThisFrame < 300) {
        LightNode node = m_lightRemoveQueue.front();
        m_lightRemoveQueue.pop_front();

        removeLightStep(node);
        updatesThisFrame++;
    }

    // Regenerate dirty chunk meshes (max 10 per frame)
    regenerateDirtyChunks(10);
}
```

### Chunk Boundary Handling

**Problem**: Light crosses chunk boundaries

**Solution**: Mark neighbor chunks dirty

```cpp
void LightingSystem::setSkyLight(const glm::ivec3& worldPos, uint8_t value) {
    auto [chunkX, chunkY, chunkZ] = worldToChunkCoords(worldPos);
    Chunk* chunk = m_world->getChunkAt(chunkX, chunkY, chunkZ);

    auto [localX, localY, localZ] = worldToLocalCoords(worldPos);
    chunk->setSkyLight(localX, localY, localZ, value);
    chunk->markLightingDirty();

    // If on edge, mark neighbors dirty
    if (localX == 0 || localX == Chunk::WIDTH - 1 ||
        localY == 0 || localY == Chunk::HEIGHT - 1 ||
        localZ == 0 || localZ == Chunk::DEPTH - 1) {
        markNeighborChunksDirty(chunk);
    }
}
```

### Deferred Mesh Regeneration

**Problem**: Every light change triggers mesh regeneration

**Solution**: Batch updates

```cpp
void LightingSystem::regenerateDirtyChunks(int maxPerFrame) {
    int regenerated = 0;
    for (Chunk* chunk : m_dirtyChunks) {
        if (regenerated >= maxPerFrame) break;

        chunk->generateMesh(m_world);
        chunk->createVertexBuffer(m_renderer);
        regenerated++;
    }
    m_dirtyChunks.clear();
}
```

### Word-Level Parallelism (Advanced - Optional)

From 0fps.net blog - for when profiling shows light updates as bottleneck:

```cpp
// Pack 8 light values into one uint32_t (4 bits each)
inline void setPackedLight(uint32_t& packed, int index, uint8_t value) {
    int shift = index * 4;
    packed = (packed & ~(0xF << shift)) | ((value & 0xF) << shift);
}

// Decrement all 8 values simultaneously (saturate at 0)
inline uint32_t decrementAllSaturate(uint32_t packed) {
    // Complex bit manipulation - see 0fps.net article
    uint32_t odd = packed & 0xF0F0F0F0;
    uint32_t even = packed & 0x0F0F0F0F;
    // ... bit tricks ...
    return result;
}
```

---

## TESTING & VALIDATION

### Unit Tests

```cpp
// tests/test_lighting.cpp

TEST(Lighting, SunlightPropagatesDown) {
    World world(10, 10, 10);
    LightingSystem lighting(&world);

    for (int y = 0; y < 10; y++) {
        world.setBlock(5, y, 5, BLOCK_AIR);
    }

    lighting.initializeWorldLighting();

    for (int y = 0; y < 10; y++) {
        EXPECT_EQ(lighting.getSkyLight({5, y, 5}), 15);
    }
}

TEST(Lighting, TorchLightDecays) {
    World world(20, 20, 20);
    LightingSystem lighting(&world);

    glm::ivec3 torchPos(10, 10, 10);
    lighting.addLightSource(torchPos, 14);

    EXPECT_EQ(lighting.getBlockLight(torchPos), 14);
    EXPECT_EQ(lighting.getBlockLight(torchPos + glm::ivec3(1,0,0)), 13);
    EXPECT_EQ(lighting.getBlockLight(torchPos + glm::ivec3(2,0,0)), 12);
    EXPECT_EQ(lighting.getBlockLight(torchPos + glm::ivec3(14,0,0)), 0);
}

TEST(Lighting, LightRemovalWorks) {
    World world(20, 20, 20);
    LightingSystem lighting(&world);

    glm::ivec3 torchPos(10, 10, 10);
    lighting.addLightSource(torchPos, 14);

    EXPECT_GT(lighting.getBlockLight(torchPos + glm::ivec3(5,0,0)), 0);

    lighting.removeLightSource(torchPos);

    EXPECT_EQ(lighting.getBlockLight(torchPos), 0);
    EXPECT_EQ(lighting.getBlockLight(torchPos + glm::ivec3(5,0,0)), 0);
}
```

### Visual Tests

1. **Sunlight Columns**: Dig straight down, verify full brightness
2. **Torch Sphere**: Place torch in cave, verify ~14-block radius
3. **Shadow Cast**: Place opaque block between torch and wall
4. **Light Blending**: Walk through lit/dark boundary
5. **Cave Lighting**: Winding cave with scattered torches

### Performance Tests

```cpp
TEST(LightingPerf, MassiveLightChange) {
    World world(50, 50, 50);
    LightingSystem lighting(&world);

    auto start = std::chrono::high_resolution_clock::now();

    // Place 100 torches
    for (int i = 0; i < 100; i++) {
        lighting.addLightSource(randomPosition(), 14);
    }

    // Process all updates
    while (!lighting.queuesEmpty()) {
        lighting.update(0.016f);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // Should complete in < 1 second
    EXPECT_LT(ms.count(), 1000);
}
```

---

## IMPLEMENTATION PHASES

### Phase 1: Foundation (Week 1)
- [ ] Create LightingSystem class skeleton
- [ ] Add BlockLight struct
- [ ] Implement Chunk light storage
- [ ] Add get/set accessors
- [ ] Write basic unit tests

### Phase 2: Sunlight (Week 2)
- [ ] Initial sunlight generation (top-down)
- [ ] BFS sunlight propagation
- [ ] Horizontal spread with decay
- [ ] Test with simple world

### Phase 3: Torch Light (Week 3)
- [ ] BFS torch light propagation
- [ ] Add emissive block properties
- [ ] Test torch placement/removal
- [ ] Integrate with block placement

### Phase 4: Light Removal (Week 4)
- [ ] Two-queue removal algorithm
- [ ] Re-propagation from remaining sources
- [ ] Handle chunk boundaries
- [ ] Performance testing

### Phase 5: Smooth Lighting (Week 5)
- [ ] Per-vertex light calculation
- [ ] Ambient occlusion integration
- [ ] Modify Vertex struct
- [ ] Update mesh generation

### Phase 6: Shader Integration (Week 6)
- [ ] Update vertex shader
- [ ] Update fragment shader
- [ ] Test smooth transitions
- [ ] Visual validation

### Phase 7: Optimization (Week 7)
- [ ] Incremental updates
- [ ] Chunk boundary handling
- [ ] Deferred mesh regeneration
- [ ] Profiling and tuning

### Phase 8: Polish (Week 8)
- [ ] Bug fixes
- [ ] Edge case handling
- [ ] Performance testing
- [ ] Documentation

---

## SOURCES & REFERENCES

### Primary Sources

1. **0 FPS - Voxel Lighting**
   - URL: https://0fps.net/2018/02/21/voxel-lighting/
   - Key: Word-level parallelism, multi-directional sunlight
   - Used for: Advanced optimizations

2. **0 FPS - Ambient Occlusion for Minecraft-like Worlds**
   - URL: https://0fps.net/2013/07/03/ambient-occlusion-for-minecraft-like-worlds/
   - Key: Per-vertex AO, quad orientation fix
   - Used for: Smooth lighting implementation

3. **GameDev StackExchange - Minecraft-Style Voxel Sunlight**
   - URL: https://gamedev.stackexchange.com/questions/170011/minecraft-style-voxel-sunlight-algorithm
   - Key: BFS approach, Dijkstra-based propagation
   - Used for: Core algorithm design

4. **GameDev StackExchange - Light Removal Algorithm**
   - URL: https://gamedev.stackexchange.com/questions/109347/how-to-remove-voxel-lights-with-minecraft-style-algorithm
   - Key: Two-queue removal, re-propagation
   - Used for: Handling ghost lighting

5. **Seeds of Andromeda - Fast Flood Fill Lighting**
   - URL: https://www.seedofandromeda.com/blogs/29-fast-flood-fill-lighting-in-a-blocky-voxel-game-pt-1
   - Key: Complete C++ implementation
   - Used for: Reference implementation

6. **Minecraft Wiki - Light**
   - URL: https://minecraft.wiki/w/Light
   - Key: Official light levels, block properties
   - Used for: Light level definitions

7. **GitHub - PocketMine Lighting Algorithm Spec**
   - URL: https://github.com/dktapps/lighting-algorithm-spec
   - Key: Formal specification
   - Used for: Edge case handling

8. **ClassicCube Source Code**
   - URL: https://github.com/UnknownShadow200/ClassicCube
   - Key: Open-source reference (src/Lighting.c)
   - Used for: Practical C implementation

### Academic & Advanced

9. **Deferred Voxel Shading for Real Time Global Illumination**
   - URL: https://jose-villegas.github.io/post/deferred_voxel_shading/
   - Key: Advanced GI techniques
   - Status: Future enhancement

10. **Voxel Cone Tracing**
    - Various academic papers
    - Key: Real-time GI using voxel grids
    - Status: Out of scope for MVP

---

## NOTES & CONSIDERATIONS

### Potential Issues

1. **Thread Safety**: Lighting system must be thread-safe for world streaming
   - Solution: Use mutex or atomic operations

2. **Save/Load**: Light data must persist with chunks
   - Solution: Add to chunk serialization

3. **Dynamic Time of Day**: Current plan uses static sunlight
   - Solution: Multiply sky light by time-of-day factor in shader

4. **Colored Lighting**: Start with greyscale (1 byte), upgrade to RGB later
   - Future: 4x storage (4 bytes per block)

### Future Enhancements

1. Volumetric lighting (god rays)
2. Bloom effect for bright sources
3. HDR lighting (>15 light levels)
4. Global illumination (voxel cone tracing)
5. Caustics (water surface patterns)
6. Colored lighting (RGB channels)

---

## APPROVAL REQUIRED BEFORE IMPLEMENTATION

This plan is complete but requires user approval before beginning implementation.

**Review Checklist**:
- [ ] Data structures (Section 3)
- [ ] Algorithms (Section 4)
- [ ] Integration points (Section 6)
- [ ] Performance targets (Section 8)
- [ ] Timeline (Section 10: 8 weeks)

**Questions? Changes? Approval?**

---

**END OF LIGHTING SYSTEM PLAN**
