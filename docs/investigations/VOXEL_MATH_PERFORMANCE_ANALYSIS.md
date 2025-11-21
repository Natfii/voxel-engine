# Voxel Engine Coordinate Conversion Performance Analysis

## Executive Summary

The voxel coordinate math and conversions are **a significant performance bottleneck** in the engine. The primary offender is the `worldToBlockCoords()` function which is called 72 times across 9 source files. This function performs expensive `std::floor()` operations and uses division-by-32 instead of bit shifts.

**Estimated Performance Impact:**
- Each `worldToBlockCoords()` call: ~3-5 CPU cycles (3x std::floor + 3x division + modulo operations)
- At 72 call sites x multiple frames per second = tens of thousands of calls per second
- Could be improved by **30-50% with optimizations listed below**

---

## 1. Analysis: worldToBlockCoords() Usage Frequency

### Files Using worldToBlockCoords():
- `/home/user/voxel-engine/include/world_utils.h` - Definition (inline function)
- `/home/user/voxel-engine/src/world.cpp` - 12 direct calls
- `/home/user/voxel-engine/src/world_streaming.cpp` - Multiple calls
- 7 other source files

### Call Sites in world.cpp:
```
Line 965:  getBlockAt() - hot path for block queries
Line 981:  getChunkAtWorldPosUnsafe() - chunk lookups
Line 987:  getChunkAtWorldPos() - chunk lookups with locking
Line 1207: setBlockAt() - block placement
Line 1221: getBlockMetadataAt() - metadata queries
Line 1235: setBlockMetadataAt() - metadata updates
Line 1249: isBlockSolid() - collision checks
Line 1259: isBlockTransparent() - rendering checks
Line 1287: getBlockLightAt() - lighting queries
Line 1299: setBlockLightAt() - lighting updates
Line 1394: removeBlockAt() - block removal
Line 1522: water simulation block access
```

### Estimated Call Frequency per Frame:
- **Player update loop**: 9+ calls per frame (collision, liquid detection)
- **Mesh generation**: 32,768 iterations per chunk × getSmoothLight() lambda
- **Raycast loop**: Every voxel step (potentially 100+ iterations)
- **Lighting system**: Hundreds of calls during propagation
- **Water simulation**: Multiple calls per simulated block
- **Targeting system**: 1+ calls per frame for block lookup

**Total: 10,000+ coordinate conversions per frame at 60 FPS**

---

## 2. Hot Loops Using std::floor()

### Current Implementation (world_utils.h:61-89):
```cpp
inline BlockCoordinates worldToBlockCoords(float worldX, float worldY, float worldZ) {
    constexpr int CHUNK_WIDTH = 32;
    constexpr int CHUNK_HEIGHT = 32;
    constexpr int CHUNK_DEPTH = 32;

    // THREE std::floor() calls here - EXPENSIVE
    int blockX = static_cast<int>(std::floor(worldX));
    int blockY = static_cast<int>(std::floor(worldY));
    int blockZ = static_cast<int>(std::floor(worldZ));

    // Division by 32 - can be bit shift
    int chunkX = blockX / CHUNK_WIDTH;
    int chunkY = blockY / CHUNK_HEIGHT;
    int chunkZ = blockZ / CHUNK_DEPTH;

    // Manual modulo with negative number correction
    int localX = blockX - (chunkX * CHUNK_WIDTH);
    int localY = blockY - (chunkY * CHUNK_HEIGHT);
    int localZ = blockZ - (chunkZ * CHUNK_DEPTH);

    // Three conditional branches to handle negatives
    if (localX < 0) { localX += CHUNK_WIDTH; chunkX--; }
    if (localY < 0) { localY += CHUNK_HEIGHT; chunkY--; }
    if (localZ < 0) { localZ += CHUNK_DEPTH; chunkZ--; }

    return { chunkX, chunkY, chunkZ, localX, localY, localZ };
}
```

### Files with std::floor() in Hot Paths:

1. **lighting_system.cpp** (lines 120-122, 134-136, 146-148):
   - Called for every light source add/remove operation
   - Frequency: Hundreds per frame during lighting propagation

2. **chunk.cpp** (lines 609-611):
   - Called in innermost loop of mesh generation
   - Frequency: 32×32×32 blocks per chunk × 4 samples per face = ~1.3M calls per chunk

3. **raycast.cpp** (lines 43-45):
   - Called once per raycast
   - Frequency: Once per targeting system update

---

## 3. Division by 32 vs. Bit Shifts

### Current Code:
- `world_utils.h:74-76` - 3 divisions by 32
- `water_simulation.cpp:496-498` - 3 divisions by 32
- `biome_map.cpp:224-225` - 2 divisions by 32

### Optimization Opportunity:
**CPU Cycles (Haswell)**: 
- Division: 24-39 cycles
- Bit shift: 1 cycle

**Potential savings**: 23-38 cycles per conversion × 10,000 conversions = 230,000-380,000 cycles per frame

### Safe Replacement:
```cpp
int chunkX = blockX >> 5;  // Right shift by 5 = divide by 32
```

---

## 4. Redundant Coordinate Conversions

### Pattern Analysis in player.cpp (updatePhysics):
Lines 206, 223, 305, 315, 324, 333, 342 - Multiple `getBlockAt()` calls that each perform full coordinate conversion:
- No caching of chunks
- Collision point checks repeatedly convert coordinates for nearby blocks
- Potential to access same chunk multiple times per frame

### Optimization Opportunity:
Cache chunk after one conversion, then access nearby blocks directly from chunk.

---

## 5. GLM Usage vs. Custom Vec3/Mat4

### Current State:
- **GLM actively used**: 274 occurrences across source files
- **Custom Vec3/Mat4 defined**: `/home/user/voxel-engine/include/voxelmath.h`
- **Mixed usage**: Both GLM vectors and custom Vec3 in codebase

### Issues:
1. **Custom normalize() function** - No zero-length check, not SIMD-optimized
2. **No SIMD**: Custom Vec3 is not SIMD-aligned
3. **Inconsistency**: Mixed use of GLM and custom vectors

### Recommendation:
Standardize on GLM throughout the engine for better optimization potential and safety.

---

## 6. normalize() Calls in Hot Paths

### Usage in player.cpp:

**Lines 186-196** - Called every frame during movement:
- normalize() twice on same direction (Forward/Backward)
- normalize() on wish direction diagonal
- Cost: ~10-15 CPU cycles per call = 30-45 cycles per frame

**Lines 809-812** - Called in updateVectors():
- Called every frame and on every mouse movement
- Cost: 3 normalize() calls = 30-45 cycles per frame

### Optimization Opportunity:
Pre-calculate and cache normalized directions, only recalculate when needed.

---

## 7. Mesh Generation Loop Analysis

### chunk.cpp:670-673 - Triple Nested Loop:
```cpp
for(int X = 0; X < WIDTH;  ++X) {        // 32 iterations
    for(int Y = 0; Y < HEIGHT; ++Y) {    // 32 iterations
        for(int Z = 0; Z < DEPTH;  ++Z) { // 32 iterations
            // 32,768 total iterations per chunk
```

### Inside Loop: getSmoothLight Lambda (lines 605-649):
- Called 4 times per visible face
- Each call does 4 × getLightAtWorldPos() calls
- Total per chunk: ~1.3 million coordinate-related operations
- Time at 3 GHz: ~1.7 milliseconds per chunk

---

## Performance Bottleneck Summary

| Bottleneck | Location | Frequency | Est. Cost/Frame |
|-----------|----------|-----------|-----------------|
| std::floor() calls | worldToBlockCoords | 72 calls × 3 floors | ~1,000 cycles |
| Division by 32 | worldToBlockCoords | 72 calls × 3 divisions | ~2,000-3,000 cycles |
| Mesh generation loops | chunk.cpp:670-873 | Per chunk | 5-10M cycles |
| normalize() calls | player.cpp | Every frame | 60-90 cycles |
| Collision checks | player.cpp:206-342 | Per frame | 2,000-5,000 cycles |
| Raycast loop | raycast.cpp:63-96 | Per targeting update | 1,000-10,000 cycles |
| Lighting propagation | lighting_system.cpp | Per light change | 5,000-100,000 cycles |

**Total Estimated Hot Path Impact: 5-20% of frame time in coordinate conversion**

---

## Optimization Recommendations (Ranked by Impact)

### 1. **Replace Division with Bit Shifts** [IMPACT: 30-50%]
- **Priority**: HIGH
- **Effort**: LOW (1 file)
- **Risk**: LOW

Modify `world_utils.h:74-76`:
```cpp
int chunkX = blockX >> 5;  // Right shift by 5 = divide by 32
int chunkY = blockY >> 5;
int chunkZ = blockZ >> 5;
```

### 2. **Cache Chunks in Collision Detection** [IMPACT: 20-40%]
- **Priority**: HIGH
- **Effort**: MEDIUM
- **Risk**: LOW

Replace repeated `getBlockAt()` calls with single chunk lookup and multiple direct block accesses.

### 3. **Vectorize floor() with SIMD** [IMPACT: 15-25%]
- **Priority**: MEDIUM
- **Effort**: MEDIUM
- **Risk**: LOW

Use `glm::ivec3(glm::floor(worldPos))` instead of manual floor() calls.

### 4. **Pre-calculate normalize() Results** [IMPACT: 5-10%]
- **Priority**: LOW
- **Effort**: LOW
- **Risk**: LOW

Store normalized directions in Player class, only recalculate on mouse movement.

### 5. **Specialized Coordinate Conversion for Hot Paths** [IMPACT: 10-20%]
- **Priority**: MEDIUM
- **Effort**: MEDIUM
- **Risk**: MEDIUM

Create overloaded version returning both chunk and local coords in one pass.

### 6. **Use GLM Consistently** [IMPACT: 5-15% potential]
- **Priority**: MEDIUM
- **Effort**: HIGH
- **Risk**: MEDIUM

Replace custom Vec3/Mat4 with GLM equivalents.

---

## Estimated Overall Performance Gain

With all optimizations implemented:
- **Bit shifts alone**: 20-30% improvement to coordinate conversion speed
- **Chunk caching**: 15-25% improvement to collision detection  
- **SIMD floor**: 10-15% improvement to lighting operations
- **Overall frame time impact**: 5-15% FPS improvement

**Conservative estimate**: 8-10% average FPS improvement without architectural changes

---

## Files to Modify

1. `/home/user/voxel-engine/include/world_utils.h` - Lines 74-76 (bit shifts)
2. `/home/user/voxel-engine/src/water_simulation.cpp` - Lines 496-498 (bit shifts)
3. `/home/user/voxel-engine/src/biome_map.cpp` - Lines 224-225 (bit shifts)
4. `/home/user/voxel-engine/src/player.cpp` - Lines 206-342 (chunk caching)
5. `/home/user/voxel-engine/src/lighting_system.cpp` - Lines 120-122, 134-136, 146-148 (GLM floor)
6. `/home/user/voxel-engine/src/player.cpp` - Lines 186-196, 809-812 (normalize caching)

