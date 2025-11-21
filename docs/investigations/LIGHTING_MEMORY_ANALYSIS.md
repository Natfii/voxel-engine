# Lighting System Memory Investigation Report

## Executive Summary
The lighting system uses efficient memory-packed structures (1 byte per block for lighting), but has notable inefficiencies with interpolated lighting (256 KB per chunk). Memory cleanup on chunk unload is properly implemented, but there are several areas with potential for improvement.

---

## 1. BlockLight Data Memory Usage Per Chunk

### Expected: 32 KB ✓ CORRECT

**Structure:**
```cpp
struct BlockLight {
    uint8_t skyLight   : 4;  // 4 bits
    uint8_t blockLight : 4;  // 4 bits
}; // Total: 1 byte
```

**Calculation:**
- Blocks per chunk: 32 × 32 × 32 = 32,768 blocks
- Memory per block: 1 byte
- **Memory per chunk: 32,768 bytes = 32 KB** ✓

**Location in code:**
- Defined: `/home/user/voxel-engine/include/block_light.h`
- Stored: `Chunk::m_lightData` (line 533 of chunk.h)
- Data type: `std::array<BlockLight, WIDTH * HEIGHT * DEPTH>`
- Initialization: `m_lightData.fill(BlockLight(0, 0))` in constructor (chunk.cpp:82)

**Verification:**
- Static assert: `static_assert(sizeof(BlockLight) == 1, "BlockLight must be exactly 1 byte");`
- This ensures compilation will fail if packing is incorrect

### **FINDINGS:**
- ✓ Memory usage is exactly as designed
- ✓ Uses bit-packing efficiently for two 4-bit channels
- ✓ No memory waste on this structure

---

## 2. Interpolated Lighting Memory Usage

### WARNING: 256 KB Per Chunk (8x overhead vs BlockLight)

**Structure:**
```cpp
struct InterpolatedLight {
    float skyLight;    // 4 bytes
    float blockLight;  // 4 bytes
}; // Total: 8 bytes
```

**Calculation:**
- Blocks per chunk: 32,768
- Memory per block: 8 bytes (2 floats)
- **Memory per chunk: 262,144 bytes = 256 KB**

**Location in code:**
- Declared: Line 542 in `/home/user/voxel-engine/include/chunk.h`
- Data type: `std::array<InterpolatedLight, WIDTH * HEIGHT * DEPTH>`
- Initialization: `m_interpolatedLightData.fill(InterpolatedLight())` in constructor (chunk.cpp:85)

**Purpose:**
Smooth time-based lighting transitions for day/night cycles or dynamic light changes.

### **ISSUES IDENTIFIED:**

1. **Memory Overhead**: 8 times larger than BlockLight data
   - Per chunk: 256 KB vs 32 KB
   - For 500 chunks: 128 MB just for interpolated lighting
   
2. **Duplication**: Stores two separate copies of lighting state
   - BlockLight (32 KB): Source of truth
   - InterpolatedLight (256 KB): Smoothed version
   
3. **No Memory Pooling**: Unlike BlockLight which is compact, these floats waste memory

**Recommendation:**
Consider using lower precision for interpolation (e.g., uint8_t instead of float) to reduce memory by 75%

---

## 3. m_lightAddQueue Memory Usage

### Data Structure:
```cpp
std::deque<LightNode> m_lightAddQueue;
```

**LightNode Size:**
- `glm::ivec3 position`: 12 bytes (3 × int32_t)
- `uint8_t lightLevel`: 1 byte
- `bool isSkyLight`: 1 byte
- **Total: 16 bytes** (with 2 bytes padding)

**Memory Usage Patterns:**

| Scenario | Items | Memory | Notes |
|----------|-------|--------|-------|
| Empty | 0 | ~48 bytes | Base deque overhead |
| Typical (mid-propagation) | 100 | 1.6 KB | Normal gameplay |
| Max per frame | 500 | 8.0 KB | MAX_LIGHT_ADDS_PER_FRAME |
| Unbounded growth | 10,000 | 160 KB | Theoretical worst case |

**Worst Case Scenario:**
- Massive torch/light placement near chunk boundaries
- Light propagates to 6 neighbors each frame, each neighbor adds 6 more
- Exponential growth until processing catches up: 6^n nodes

**Actual Code Limits:**
```cpp
while (!m_lightAddQueue.empty() && addCount < MAX_LIGHT_ADDS_PER_FRAME) {
    // Process 500 nodes max per frame
    addCount++;
}
```
- Frame-rate bounded: At 60 FPS with 500 nodes/frame = 30,000 nodes/second capacity
- If additions > 30,000/sec: Queue grows unbounded

### **FINDINGS:**
- ✓ Normal gameplay: ~2 KB
- ⚠ No explicit size limit - could grow to hundreds of MB if light propagation is slow
- ⚠ If frame time exceeds 16ms, queue will accumulate faster than it drains

---

## 4. m_lightRemoveQueue Memory Usage

### Data Structure:
```cpp
std::deque<LightNode> m_lightRemoveQueue;
```

**Memory Usage:**
- Same LightNode size: 16 bytes
- Max per frame: 300 nodes
- **Typical memory: 4.8 KB**
- **Max memory: 160 KB** (if unbounded to 10k nodes)

**Priority: Higher than add queue**
- Removed lights appear worse visually, so removal processing takes priority
- MAX_LIGHT_REMOVES_PER_FRAME = 300

### **FINDINGS:**
- ✓ Reasonable size for typical cases
- ⚠ Same unbounded growth risk as addQueue if many torches removed simultaneously

---

## 5. m_dirtyChunks Set Memory Usage

### Data Structure:
```cpp
std::unordered_set<Chunk*> m_dirtyChunks;
```

**Memory per entry:**
- Chunk pointer: 8 bytes (64-bit)
- Hash table overhead: ~32 bytes per entry (hash map bucket data)
- **Total per chunk: ~40 bytes**

**Memory Usage:**

| Chunks | Memory | Scenario |
|--------|--------|----------|
| 10 | 400 bytes | Small area, lighting change |
| 100 | 4 KB | Large lighting update |
| 500 | 20 KB | Massive light change |
| 1000 | 40 KB | Theoretically unbounded |

**Insertion Locations:**
1. `setSkyLight()` - line 239
2. `setBlockLight()` - line 264
3. `markNeighborChunksDirty()` - lines 298, 304, 312, 318, 326, 332
4. `recalculateViewportLighting()` - line 612

**Removal Locations:**
1. `notifyChunkUnload()` - line 146
2. `regenerateDirtyChunks()` - lines 359, 364

**Growth Analysis:**
- Setting light at chunk boundary marks up to 7 chunks dirty (itself + 6 neighbors)
- ~500 light updates per frame can mark 3500 chunk-dirty operations
- With ~500 total chunks, the set reaches full saturation quickly

### **FINDINGS:**
- ✓ Properly cleaned up on chunk unload via `notifyChunkUnload()`
- ✓ Automatically cleared when chunks regenerate
- ✓ Memory is reasonable (20 KB for 500 dirty chunks)
- ✓ No memory leaks detected in dirty chunk tracking

---

## 6. Local Stack Allocations in removeLightStep()

### CRITICAL: Potential Stack Overflow Risk

**Code (lines 523-524):**
```cpp
std::deque<LightNode> removalQueue;    // Stack allocation
std::deque<LightNode> addBackQueue;    // Stack allocation
```

**Worst Case Analysis:**

**Scenario: Removing a light source at max strength (15)**
- Light affects sphere of radius 15 around source
- Volume: 4/3 × π × 15³ ≈ 14,137 blocks
- Memory needed: 14,137 blocks × 16 bytes = 226 KB

**Stack Space Available:**
- Typical C++ stack: 1-8 MB per thread
- Default in this project: Unknown, likely 2-4 MB
- 226 KB is ~5-10% of typical stack: **Generally Safe**

**Actual Risk:**
- Multiple light removals in same frame
- Each removalQueue can grow independently
- If two large removals occur: 452 KB (still safe, but concerning)
- If three large removals: 678 KB (pushing limits)

**Exacerbating Factor:**
```cpp
while (!m_lightRemoveQueue.empty() && removeCount < MAX_LIGHT_REMOVES_PER_FRAME) {
    LightNode node = m_lightRemoveQueue.front();
    m_lightRemoveQueue.pop_front();
    removeLightStep(node);  // <-- Creates new deques for EACH removal
    removeCount++;
}
```
- Up to 300 removals per frame
- Each removal can allocate 226 KB on stack
- Max concurrent: Only 1 removal function at a time (sequential), so stack reused

### **FINDINGS:**
- ⚠ Stack allocation is not ideal, but generally safe
- ⚠ Should consider moving to heap allocation if removals are large
- ✓ Sequential processing prevents stack overflow
- ✓ RAII ensures cleanup if exceptions occur

---

## 7. Chunk Unload and Lighting Data Cleanup

### Unload Process (world.cpp:1130-1175)

**Sequence:**
1. Find chunk in map
2. Remove from pending decorations
3. Remove from chunks vector
4. **CALL notifyChunkUnload() - Line 1153**
5. Destroy Vulkan buffers
6. Move to cache or pool

**notifyChunkUnload() Implementation (lighting_system.cpp:140-147):**
```cpp
void LightingSystem::notifyChunkUnload(Chunk* chunk) {
    if (!chunk) return;
    // Remove chunk from dirty chunks to prevent dangling pointer access
    m_dirtyChunks.erase(chunk);
}
```

### **FINDINGS:**
- ✓ notifyChunkUnload() is called BEFORE chunk destruction
- ✓ Properly removes chunk from m_dirtyChunks set
- ✓ Prevents dangling pointers
- ✓ Comment explains the critical need for this call
- ✓ No memory leaks detected in chunk unload

**Potential Issue:**
- The m_lightAddQueue and m_lightRemoveQueue can still contain pointers to unloaded chunks
- Solution: Check if chunk exists before processing in propagateLightStep/removeLightStep
- Currently handled: Both functions check `if (!chunk) return;` at line 432 and 426

### Memory Verification:

**When chunk is unloaded:**
1. BlockLight data: Freed by unique_ptr in chunk destructor ✓
2. Interpolated lighting: Freed by unique_ptr in chunk destructor ✓
3. m_lightData array: Cleaned automatically ✓
4. m_interpolatedLightData array: Cleaned automatically ✓

---

## 8. Memory Allocation Hotspots

### Analysis of Dynamic Allocations:

**LightingSystem.cpp Allocations:**

1. **Line 584: getVisibleChunks()**
```cpp
std::vector<Chunk*> visibleChunks;
// Adds ~60-100 chunks during runtime
```
- **Impact**: ~500-800 bytes per call
- **Frequency**: Once per recalculateViewportLighting() call
- **Risk**: Minimal - vector is stack allocated and short-lived

2. **Line 523-524: removeLightStep() local deques**
```cpp
std::deque<LightNode> removalQueue;
std::deque<LightNode> addBackQueue;
```
- **Impact**: Grows with removal area size
- **Frequency**: Up to 300 times per frame (MAX_LIGHT_REMOVES_PER_FRAME)
- **Risk**: Moderate - stack allocations, but sequential

3. **Constructor/Initialization (chunk.cpp:82-85)**
```cpp
m_lightData.fill(BlockLight(0, 0));
m_interpolatedLightData.fill(InterpolatedLight());
```
- **Impact**: One-time per chunk creation
- **Frequency**: During world load or chunk generation
- **Risk**: None - efficient initialization

**No Explicit new/delete in Lighting System**
- All allocations use STL containers with RAII
- No memory leaks from manual pointers

---

## 9. Memory Leak Analysis

### Comprehensive Check:

1. **Queue Processing:**
   - ✓ Nodes dequeued in FIFO order
   - ✓ All nodes processed or cleared on update
   - ✓ No nodes left dangling

2. **Chunk Pointer Tracking:**
   - ✓ notifyChunkUnload() removes chunk from m_dirtyChunks
   - ✓ Queue nodes are checked for null chunk at processing
   - ✓ No dangling pointers in sets

3. **Local Allocations:**
   - ✓ All local deques are RAII
   - ✓ Automatic cleanup on function exit or exception
   - ✓ No manual new/delete

4. **Chunk Data:**
   - ✓ BlockLight arrays use std::array (automatic cleanup)
   - ✓ InterpolatedLight arrays use std::array (automatic cleanup)
   - ✓ Arrays freed when chunks are destroyed

### **FINDINGS:**
- ✓ **NO MEMORY LEAKS DETECTED**
- ✓ Proper use of RAII throughout
- ✓ Safe pointer handling with null checks
- ✓ Chunk cleanup is properly orchestrated

---

## 10. Performance vs Memory Tradeoffs

### Current Design Decisions:

**Good Decisions:**
1. ✓ 1-byte BlockLight storage (very efficient)
2. ✓ Deque-based queuing (efficient FIFO)
3. ✓ Unordered_set for dirty chunks (fast insertion/removal)
4. ✓ Frame-rate bounded processing (prevents memory stalls)

**Potential Improvements:**
1. ⚠ InterpolatedLight uses 8 bytes per block (256 KB/chunk)
   - Could optimize to 2 bytes (uint8_t per channel)
   - Would save 192 KB per chunk = ~96 MB for 500 chunks

2. ⚠ Local stack deques in removeLightStep could cause stack pressure
   - Worst case: 226 KB per removal
   - Recommendation: Use heap allocation if removal area > 1000 blocks

3. ⚠ Queue growth unbounded between frames
   - Recommendation: Add maximum queue size limits

---

## Summary Table

| Component | Memory | Status | Risk |
|-----------|--------|--------|------|
| BlockLight/chunk | 32 KB | ✓ Optimal | None |
| InterpolatedLight/chunk | 256 KB | ⚠ Excessive | Medium (can optimize) |
| m_lightAddQueue (typical) | 8 KB | ✓ Good | Low |
| m_lightAddQueue (worst) | 160 KB | ⚠ High | Medium |
| m_lightRemoveQueue (typical) | 4.8 KB | ✓ Good | Low |
| m_dirtyChunks (500 chunks) | 20 KB | ✓ Good | None |
| Local deques/removal (worst) | 226 KB stack | ⚠ Alert | Medium |
| **Total per chunk loaded** | 288 KB | ⚠ High | Low |
| **Total for 500 chunks** | ~140 MB | ⚠ Moderate | Low |

---

## Recommendations

1. **PRIORITY: Monitor Queue Sizes**
   - Add statistics collection for max queue sizes
   - Log warnings if queue exceeds threshold (e.g., 1000 items)

2. **OPTIMIZE InterpolatedLight**
   - Consider reducing from float (8 bytes) to uint8_t (1 byte)
   - Would reduce per-chunk memory from 288 KB to 96 KB
   - Savings: ~96 MB for 500 chunks

3. **Add Queue Size Limits**
   - Implement `MAX_QUEUE_SIZE` cap
   - Prevent unbounded memory growth
   - Discard old nodes if queue exceeds limit (with logging)

4. **Consider Heap Allocation for removeLightStep**
   - If largest removals exceed 100 blocks, use std::make_unique<std::deque>
   - Avoids stack pressure

5. **Add Memory Profiling**
   - Track actual queue sizes during gameplay
   - Log when chunks load/unload
   - Profile memory fragmentation

