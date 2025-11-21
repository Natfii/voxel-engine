# Light Propagation Queue Performance Analysis

## Executive Summary

The voxel engine's lighting system implements BFS (Breadth-First Search) flood-fill light propagation using two FIFO queues (`m_lightAddQueue` and `m_lightRemoveQueue`). While the implementation is correct algorithmically, there are **significant performance issues** during world initialization that could cause loading times to spike dramatically.

**Critical Finding:** During world load, the lighting system can queue **hundreds of millions of light nodes** that are processed in a **blocking infinite-capacity loop**, potentially blocking the loading screen for minutes on large worlds.

---

## 1. Queue Population Analysis

### 1.1 Where Queues are Populated

**File:** `/home/user/voxel-engine/src/lighting_system.cpp`

Queue nodes are added in 5 locations:

1. **Line 109:** `addLightSource()` - Block light from torches/lava
   ```cpp
   m_lightAddQueue.emplace_back(blockPos, lightLevel, false);  // Block light
   ```

2. **Line 121:** `addSkyLightSource()` - Sky light initialization
   ```cpp
   m_lightAddQueue.emplace_back(blockPos, lightLevel, true);   // Sky light
   ```

3. **Lines 158, 169, 173:** `onBlockChanged()` - Block placement/removal lighting updates
   ```cpp
   m_lightAddQueue.emplace_back(worldPos, aboveLight, true);   // Breaking opaque → sunlight floods
   m_lightRemoveQueue.emplace_back(worldPos, oldSkyLight, true);      // Placing opaque → light blocked
   m_lightRemoveQueue.emplace_back(worldPos, oldBlockLight, false);
   ```

4. **Lines 399, 406:** `generateSunlightColumn()` - **PRIMARY SOURCE DURING INITIALIZATION**
   ```cpp
   // For each transparent block from Y=320 down to surface
   m_lightAddQueue.emplace_back(blockPos, 15, true);  // Queues for horizontal propagation
   ```

5. **Line 502:** `propagateLightStep()` - Recursive propagation during BFS
   ```cpp
   m_lightAddQueue.emplace_back(neighborPos, newLight, node.isSkyLight);
   ```

6. **Line 576:** `removeLightStep()` - Re-propagation during removal algorithm
   ```cpp
   m_lightAddQueue.push_back(addBackQueue.front());
   ```

### 1.2 Queue Size During World Load - MASSIVE GROWTH

**Calculation for Spawn Chunk Initialization:**

From `/home/user/voxel-engine/src/main.cpp:462`:
- Spawn radius = 5 chunks
- Total chunks = (2×5+1)³ = 11³ = **1,331 chunks**
- Chunk dimensions = 32×32×32 blocks

**During `generateSunlightColumn()` (lines 371-419):**

For each chunk:
- Iterate 32×32 = **1,024 (x,z) column positions**
- For each column, iterate down from Y=320

For typical terrain (surface at Y≈60-100):
- Air blocks above surface: Y=320 down to Y≈100 = **~220 blocks**
- **Each air/transparent block queued:** 1,024 × 220 = **225,280 nodes per chunk**

**Total for spawn area:**
- 1,331 chunks × 225,280 nodes = **~300 million light nodes**

**Queue Memory:**
- Each `LightNode` = 3 integers + 1 byte = ~16 bytes
- 300M nodes × 16 bytes = **4.8 GB of memory** (if all stayed queued)

**Why This Is Problematic:**

1. **Unbounded Queue Growth:** During `generateSunlightColumn()`, light nodes are added without any limit
2. **Recursive Growth:** `propagateLightStep()` adds neighbors to the queue, causing potential exponential growth
3. **Blocking Processing:** During initialization, `initializeWorldLighting()` processes ALL queued nodes in a **single blocking loop** (lines 59-64)

---

## 2. Queue Behavior During Different Game Phases

### 2.1 World Initialization Phase

**Location:** `/home/user/voxel-engine/src/main.cpp:486`

```cpp
world.getLightingSystem()->initializeWorldLighting();
```

**In `LightingSystem::initializeWorldLighting()` (lines 26-67):**

```cpp
void LightingSystem::initializeWorldLighting() {
    // ... generate sunlight for all chunks ...

    // BLOCKING INFINITE-CAPACITY LOOP
    std::cout << "Processing " << m_lightAddQueue.size() << " light propagation nodes..." << std::endl;
    int processedCount = 0;
    while (!m_lightAddQueue.empty()) {  // ← NO BATCH LIMIT!
        LightNode node = m_lightAddQueue.front();
        m_lightAddQueue.pop_front();
        propagateLightStep(node);       // ← Can add up to 6 new neighbors
        processedCount++;
    }
}
```

**Problem:** This loop has **NO BATCH LIMITS** like `MAX_LIGHT_ADDS_PER_FRAME`. It will process ALL nodes before returning, potentially blocking the loading screen for minutes.

**Estimated Time for 300M nodes:**
- Cost per `propagateLightStep()`: ~100-500 CPU cycles (chunk lookup, light checks, 6 neighbor checks)
- 300M nodes × 500 cycles × (1 CPU cycle / 3 GHz) = **~50 seconds**

### 2.2 Normal Gameplay Phase

**Location:** `/home/user/voxel-engine/src/lighting_system.cpp:71-94`

```cpp
void LightingSystem::update(float deltaTime, VulkanRenderer* renderer) {
    // Process light additions (new torches, sunlight spread, etc.)
    int addCount = 0;
    while (!m_lightAddQueue.empty() && addCount < MAX_LIGHT_ADDS_PER_FRAME) {
        LightNode node = m_lightAddQueue.front();
        m_lightAddQueue.pop_front();
        propagateLightStep(node);
        addCount++;
    }

    // Process light removals (higher priority)
    int removeCount = 0;
    while (!m_lightRemoveQueue.empty() && removeCount < MAX_LIGHT_REMOVES_PER_FRAME) {
        LightNode node = m_lightRemoveQueue.front();
        m_lightRemoveQueue.pop_front();
        removeLightStep(node);
        removeCount++;
    }
}
```

**Performance Characteristics:**
- MAX_LIGHT_ADDS_PER_FRAME = 500 nodes per frame
- MAX_LIGHT_REMOVES_PER_FRAME = 300 nodes per frame
- At 60 FPS: 500×60 = 30,000 additions/second (reasonable)
- **Queues properly limited to prevent frame drops**

**Queue Status Queries (lines 209-225):**
- Public methods to check queue sizes exist: `queuesEmpty()`, `getPendingAdditions()`, `getPendingRemovals()`
- **However, NO logging during gameplay** - queue sizes are silent

### 2.3 Viewport Lighting Recalculation Phase

**Location:** `/home/user/voxel-engine/src/lighting_system.cpp:602-625`

```cpp
void LightingSystem::recalculateViewportLighting(const Frustum& frustum, const glm::vec3& playerPos) {
    // ... reinitialize visible chunks ...

    // BLOCKING LOOP - SAME ISSUE AS INITIALIZATION!
    int propagated = 0;
    while (!m_lightAddQueue.empty()) {
        LightNode node = m_lightAddQueue.front();
        m_lightAddQueue.pop_front();
        propagateLightStep(node);
        propagated++;
    }
}
```

**Problem:** This is another **blocking infinite-capacity loop** that processes all queued nodes. If the viewport contains many chunks, this could block for seconds.

---

## 3. Cost Analysis of `propagateLightStep()` - Is It O(1)?

**Location:** `/home/user/voxel-engine/src/lighting_system.cpp:424-505`

```cpp
void LightingSystem::propagateLightStep(const LightNode& node) {
    // 1. Get chunk (O(1) hash lookup)
    Chunk* chunk = m_world->getChunkAtWorldPos(...);  // O(1)
    if (!chunk) return;

    // 2. Get current light (O(1) array access with chunk lookup)
    uint8_t currentLight = node.isSkyLight ? 
        getSkyLight(node.position) :        // O(1)
        getBlockLight(node.position);       // O(1)

    // 3. Sanity checks (O(1))
    if (currentLight != node.lightLevel) return;
    if (currentLight <= 1) return;

    // 4. Loop through 6 neighbors
    for (int i = 0; i < 6; i++) {
        // 4a. Get neighbor chunk (O(1) hash lookup)
        Chunk* neighborChunk = m_world->getChunkAtWorldPos(...);  // O(1)
        if (!neighborChunk) continue;

        // 4b. Check transparency (EXPENSIVE!)
        if (!isTransparent(neighborPos)) continue;  // ← See below

        // 4c. Get neighbor light level (O(1))
        uint8_t neighborLight = node.isSkyLight ? 
            getSkyLight(neighborPos) :       // O(1)
            getBlockLight(neighborPos);      // O(1)

        // 4d. Update and queue if brighter (O(1) amortized)
        if (newLight > neighborLight) {
            setSkyLight/setBlockLight(neighborPos, newLight);  // O(1) + chunk marking
            m_lightAddQueue.emplace_back(...);                 // O(1) amortized
        }
    }
}
```

**Analysis of `isTransparent()` (lines 270-290):**

```cpp
bool LightingSystem::isTransparent(const glm::ivec3& worldPos) const {
    int blockID = m_world->getBlockAt(...);  // O(1) hash lookup

    if (blockID == BlockID::AIR) return true;

    try {
        const auto& blockDef = BlockRegistry::instance().get(blockID);  // O(1) with registry
        return blockDef.transparency > 0.0f;
    } catch (...) {
        return false;  // Unknown block, treat as opaque
    }
}
```

**Overall Complexity of `propagateLightStep()`:**

| Operation | Complexity | Count | Total |
|-----------|-----------|-------|-------|
| Chunk lookup | O(1) | 7 (1 source + 6 neighbors) | O(1) |
| Light level access | O(1) | 8 (+ potential register access) | O(1) |
| Block transparency check | O(1) | 6 neighbors | O(1) |
| Queue operation | O(1) amortized | Up to 6 | O(1) amortized |
| Chunk marking (setSkyLight/setBlockLight) | O(1) | Up to 6 | O(1) |
| **Total per node** | **O(1)** | - | **O(1)** |

**Conclusion:** `propagateLightStep()` is **O(1) per call** with constant factors.

**However, it can add up to 6 neighbors to the queue**, leading to potential exponential BFS growth:
- One light source at level 15
- Can illuminate a sphere of ~15 radius in ideal conditions
- Maximum ~4/3 * π * 15³ ≈ 14,000 blocks in one sphere
- With queue batching, this spreads over multiple frames

---

## 4. Queue Clearing and Accumulation

### 4.1 Initial Clearing

**During initialization (lines 59-64):**
```cpp
while (!m_lightAddQueue.empty()) {  // Completely drained
    LightNode node = m_lightAddQueue.front();
    m_lightAddQueue.pop_front();
    propagateLightStep(node);
}
```

**Status:** Queues are **properly drained** after `initializeWorldLighting()` completes.

### 4.2 Normal Gameplay Clearing

**During `update()` (lines 74-88):**
```cpp
// Process light additions with frame-rate limit
int addCount = 0;
while (!m_lightAddQueue.empty() && addCount < MAX_LIGHT_ADDS_PER_FRAME) {
    // ... process ...
    addCount++;
}

// Process removals with frame-rate limit
int removeCount = 0;
while (!m_lightRemoveQueue.empty() && removeCount < MAX_LIGHT_REMOVES_PER_FRAME) {
    // ... process ...
    removeCount++;
}
```

**Status:** 
- Queues **can accumulate** if additions exceed 500/frame or removals exceed 300/frame
- In typical gameplay (occasional torch placement), this is not a problem
- **However, if many blocks are modified rapidly**, queues will accumulate over frames

### 4.3 Queue Persistence Between Frames

**Problem:** Queues are **persistent** - they accumulate across frames:

1. Frame 1: Add 1000 light nodes (only 500 processed, 500 remain)
2. Frame 2: Add 500 more nodes (1000 in queue, process 500, 500 remain)
3. Frame 3: Queues continue to accumulate...

**Scenario for Accumulation:**
- Player removes a large tree structure (100+ blocks)
- Each removal queues a light removal operation
- Plus neighbors of removed blocks queue for re-propagation
- Potential 500-1000+ additions and removals queued simultaneously
- At 500/frame limit, takes multiple frames to process
- **Game remains playable, but lighting updates lag behind block modifications**

### 4.4 Missing Queue Size Logging

**Current State:** 
- Only **ONE** output during initialization (line 57):
  ```cpp
  std::cout << "Processing " << m_lightAddQueue.size() << " light propagation nodes..." << std::endl;
  ```
- **NO logging during normal gameplay** of queue sizes
- Public query methods exist but are never called for debug output

**Consequence:** Developer has **no visibility** into queue growth during actual gameplay.

---

## 5. Infinite Loops and Exponential Growth Analysis

### 5.1 Infinite Loop Detection

**Potential infinite loops found:**

1. **`initializeWorldLighting()` line 59:**
   ```cpp
   while (!m_lightAddQueue.empty()) {  // Could run for minutes
   ```
   - Not infinite if all nodes are processed
   - But will block loading for unacceptable time on large worlds

2. **`recalculateViewportLighting()` line 617:**
   ```cpp
   while (!m_lightAddQueue.empty()) {  // Could block during gameplay
   ```
   - Same issue as above

3. **`removeLightStep()` line 530:**
   ```cpp
   while (!removalQueue.empty()) {  // Local scope, won't affect main queue indefinitely
   ```
   - Processes local queue, safe
   - But could be large if removing big light source

**No true infinite loops found** - all loops are properly conditioned on queue emptiness.

### 5.2 Exponential Growth Analysis

**Scenario: Newly placed torch at light level 14**

```
Frame 1: Add torch (1 node in queue)
         Process: Add 6 neighbors at level 13 (7 nodes total)
         
Frame 2: Add 6 neighbors
         Each propagates to up to 6 new neighbors = 36 potential nodes
         But many will be duplicates or already lit
         Actual: ~30-35 new nodes
         
Frame 3: ~200 new nodes
         
Frame 4: ~500+ new nodes (hitting the 500/frame limit)
```

**Growth Pattern:**
- **NOT exponential** - light decay limits spread to ~15 blocks radius
- **Bounded growth:** Max illuminated volume per source = ~4/3 * π * 15³ ≈ 14,000 blocks
- **Actual growth:** Logarithmic due to decreasing light levels and frame-rate batching

**Conclusion:** Queue growth is **bounded and natural**, not exponential runaway.

---

## 6. Detailed Findings: Performance Issues

### Issue #1: Blocking Initialization Loop (CRITICAL)

**Severity:** CRITICAL  
**Affected:** World loading experience  
**Location:** `initializeWorldLighting()` lines 59-64 and `recalculateViewportLighting()` lines 617-622

**Problem:**
The lighting propagation loop during initialization is **unbounded** and can process hundreds of millions of nodes in a single blocking loop, freezing the loading screen.

**Evidence:**
- Spawn area: 1,331 chunks
- Estimated light nodes: ~300 million
- Processing time: ~50 seconds (blocking)

**Example Output:**
```
Completing light propagation for spawn chunks...
Processing 300000000 light propagation nodes...
[freezes for 50 seconds]
Light propagation complete!
```

**Impact:** Loading screens appear frozen for minutes on large worlds.

### Issue #2: No Queue Size Logging During Gameplay (MEDIUM)

**Severity:** MEDIUM  
**Affected:** Runtime visibility into lighting system health  
**Location:** `update()` method (no debug output)

**Problem:**
During normal gameplay, queue sizes are never logged, so developers cannot see if queues are accumulating or causing frame delays.

**Evidence:**
- Public query methods exist: `getPendingAdditions()`, `getPendingRemovals()`
- These are **never called or logged**
- No profiling data available

**Impact:** Hidden performance regressions, delayed lighting updates go unnoticed.

### Issue #3: Viewport Lighting Recalculation Blocks Gameplay (HIGH)

**Severity:** HIGH  
**Affected:** Time-of-day system responsiveness  
**Location:** `recalculateViewportLighting()` lines 602-625

**Problem:**
When sun/moon position changes and lighting must be recalculated, another **unbounded blocking loop** processes all queued nodes, potentially freezing gameplay.

**Evidence:**
- Loop at line 617-622: `while (!m_lightAddQueue.empty())`
- No batch limits
- Called when viewport lighting needs recalculation

**Impact:** Frame drops or freezes during time-of-day transitions or viewport-based lighting updates.

### Issue #4: Queue Accumulation During Rapid Block Modification (MEDIUM)

**Severity:** MEDIUM  
**Affected:** Block placement/breaking feedback latency  
**Location:** `update()` method with MAX_LIGHT_ADDS/REMOVES_PER_FRAME limits

**Problem:**
When player removes large structures or builds rapidly, lighting updates queue faster than they can be processed. Visible lag between breaking a block and seeing darkness propagate.

**Evidence:**
- Max 500 adds per frame (300 removes, higher priority)
- Each block removal can queue 2-6 light operations
- 100 blocks removed = 200-600 operations queued
- Takes 2-3 frames to process

**Impact:** Visual feedback lag for lighting updates.

---

## 7. Performance Metrics Summary

| Metric | Value | Status |
|--------|-------|--------|
| **Max spawn chunks** | 1,331 | - |
| **Est. light nodes during init** | 300 million | CRITICAL |
| **Est. init processing time** | ~50 seconds | BLOCKING |
| **Cost per `propagateLightStep()`** | O(1) constant | GOOD |
| **Max queue adds per frame** | 500 nodes | REASONABLE |
| **Max queue removes per frame** | 300 nodes | REASONABLE |
| **Light propagation sphere radius** | ~15 blocks | BOUNDED |
| **Actual queue growth** | Logarithmic | BOUNDED |
| **True infinite loops** | 0 | GOOD |
| **Queue size logging during gameplay** | None | MISSING |

---

## 8. Detailed Issue Locations

### Issue #1 - Blocking Initialization
- **File:** `src/lighting_system.cpp`
- **Lines:** 26-67 (initializeWorldLighting)
- **Root:** Line 59 `while (!m_lightAddQueue.empty())`
- **Fix Complexity:** Medium (needs progress reporting or chunking)

### Issue #2 - Viewport Blocking  
- **File:** `src/lighting_system.cpp`
- **Lines:** 602-625 (recalculateViewportLighting)
- **Root:** Line 617 `while (!m_lightAddQueue.empty())`
- **Fix Complexity:** Medium (same as Issue #1)

### Issue #3 - Missing Logging
- **File:** `src/lighting_system.cpp`
- **Lines:** 71-94 (update method)
- **Root:** No logging of queue sizes
- **Fix Complexity:** Low (just add logging statements)

### Issue #4 - Queue Accumulation
- **File:** `src/lighting_system.cpp`
- **Lines:** 71-88 (batch processing)
- **Root:** Line 74, 83 batch limits (this is actually correct)
- **Fix Complexity:** Low (queue accumulation is expected, not a bug)

---

## 9. Recommended Improvements

### Priority 1: Fix Blocking Initialization (CRITICAL)

**Option A: Add Progress Callbacks**
```cpp
void initializeWorldLighting(std::function<void(int, int)> progressCallback = nullptr) {
    // ... generate sunlight ...
    
    int totalNodes = m_lightAddQueue.size();
    int processedCount = 0;
    
    while (!m_lightAddQueue.empty()) {
        // Process in batches of 10,000
        for (int i = 0; i < 10000 && !m_lightAddQueue.empty(); i++) {
            LightNode node = m_lightAddQueue.front();
            m_lightAddQueue.pop_front();
            propagateLightStep(node);
            processedCount++;
        }
        
        // Report progress to loading screen
        if (progressCallback) {
            progressCallback(processedCount, totalNodes);
        }
    }
}
```

**Option B: Use Multi-Threading**
- Process light nodes on background threads
- Uses async thread pool
- More complex but doesn't block loading screen

### Priority 2: Add Queue Size Logging (MEDIUM)

```cpp
void LightingSystem::update(float deltaTime, VulkanRenderer* renderer) {
    // ... existing code ...
    
    // Add at the end:
    static int frameCount = 0;
    if (++frameCount % 60 == 0) {  // Log every 1 second at 60 FPS
        if (!queuesEmpty()) {
            Logger::info() << "Lighting queues - Add: " << getPendingAdditions() 
                          << ", Remove: " << getPendingRemovals();
        }
    }
}
```

### Priority 3: Fix Viewport Lighting (HIGH)

Apply same batching approach as Priority 1 to `recalculateViewportLighting()`.

### Priority 4: Monitor Accumulation (OPTIONAL)

Add profiling to detect rapid queue growth:
```cpp
if (m_lightAddQueue.size() > 5000 || m_lightRemoveQueue.size() > 3000) {
    Logger::warning() << "Large lighting queue detected - gameplay may lag";
}
```

---

## 10. Conclusion

The voxel engine's lighting system implementation is algorithmically sound with O(1) cost per propagation step. However, there are **significant performance issues** during critical game phases:

1. **Blocking initialization loops** can freeze the loading screen for 50+ seconds on large worlds
2. **Missing queue monitoring** provides no visibility into runtime behavior
3. **Viewport lighting updates** can cause in-game frame drops

The queue growth is **not exponential** and is properly limited during normal gameplay (500 adds/300 removes per frame). The natural bounded growth and frame-rate limiting are well-designed.

**Recommended action:** Implement batched progress reporting during initialization and viewport lighting updates to maintain responsive UX while preserving visual fidelity.
