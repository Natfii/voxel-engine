# Voxel Engine Block Placement/Breaking Crash Analysis

## Critical Issues Found

### 1. DEADLOCK - Recursive Mutex Locking (HIGH SEVERITY)
**Location:** `/home/user/voxel-engine/src/world.cpp` lines 818-928

**Problem:**
- `breakBlock()` and `placeBlock()` acquire a UNIQUE lock on `m_chunkMapMutex` (line 821, 945)
- Then call `generateMesh(this)` (lines 885, 919, 969, 1002)
- `generateMesh()` in chunk.cpp calls `world->getBlockAt()` (lines 461, 477)
- `getBlockAt()` tries to acquire a SHARED lock on the SAME `m_chunkMapMutex` (line 603)
- `std::shared_mutex` is NOT reentrant - this causes DEADLOCK!

**Code Flow:**
```
breakBlock() 
  -> unique_lock(m_chunkMapMutex)           [LOCKED]
  -> affectedChunk->generateMesh(this)
    -> world->getBlockAt(...)
      -> shared_lock(m_chunkMapMutex)       [DEADLOCK! Already have unique lock]
```

**Impact:** Every block break/place operation DEADLOCKS when mesh generation accesses neighboring chunks.

**Fix:** Use getBlockAtUnsafe() or getChunkAtWorldPosUnsafe() in generateMesh() since caller already holds the lock.

---

### 2. Out-of-Bounds Registry Access (HIGH SEVERITY)
**Location:** `/home/user/voxel-engine/src/chunk.cpp` line 493

**Problem:**
```cpp
int id = m_blocks[X][Y][Z];
if (id == 0) continue;
const BlockDefinition& def = registry.get(id);  // LINE 493 - NO BOUNDS CHECK!
```

Also occurs in:
- `chunk.cpp` lines 466, 482 (isSolid and isLiquid lambdas)
- `water_simulation.cpp` lines 426, 446
- `world.cpp` lines 827, 865, 960, 1060, 1082, 1107, 1132, 1146
- `block_system.cpp` lines 810, 835, 873
- `player.cpp` lines 307, 317, 326, 335, 344 (some have try-catch, others don't)
- `inventory.cpp` lines 220, 322
- `targeting_system.cpp` line 176

**Why it crashes:**
- BlockID could be corrupted/invalid
- `registry.count()` is size, valid IDs are 0 to count()-1
- Accessing out-of-bounds throws exception

**Fix:** Add bounds check before registry.get():
```cpp
if (blockID < 0 || blockID >= registry.count()) {
    // Handle invalid block
    continue; // or return default value
}
const BlockDefinition& def = registry.get(blockID);
```

---

### 3. Null Pointer Dereference in Chunk Updates (MEDIUM SEVERITY)
**Location:** `/home/user/voxel-engine/src/world.cpp` lines 882-927

**Problem:**
```cpp
Chunk* affectedChunk = getChunkAtWorldPosUnsafe(worldX, worldY, worldZ);
if (affectedChunk) {  // Check exists
    affectedChunk->generateMesh(this);
    affectedChunk->createVertexBuffer(renderer);
}

// Later, neighbors might be nullptr:
Chunk* neighbors[6] = { ... };
for (int i = 0; i < 6; i++) {
    if (neighbors[i] && neighbors[i] != affectedChunk) {  // Good null check
        neighbors[i]->generateMesh(this);
```

Actually this has proper null checks, but the deadlock happens BEFORE the null check matters.

---

### 4. Race Condition - Concurrent Chunk Modification (MEDIUM-HIGH SEVERITY)
**Location:** `/home/user/voxel-engine/src/world.cpp` and `/home/user/voxel-engine/src/chunk.cpp`

**Problem:**
- breakBlock() locks the mutex to update the block
- But generateMesh() reads from multiple neighboring chunks
- If a neighboring chunk is being modified by another thread, data race occurs
- isSolid/isLiquid lambdas at chunk.cpp:453 and 470 call world->getBlockAt() which acquires shared locks

**Race Scenario:**
1. Thread A: breakBlock() - unique lock, modifies chunk blocks
2. Thread A: calls generateMesh() - needs read access to neighbors
3. Thread B: tries to placeBlock() at neighbor - wants unique lock - BLOCKED
4. If Thread B gets the lock, it modifies neighbor data while Thread A is reading it

**Issue:** shared_lock allows multiple readers, but we need exclusive access during mesh generation after modification.

---

### 5. Missing Validation - Uninitialized Block IDs (MEDIUM SEVERITY)
**Location:** `/home/user/voxel-engine/src/main.cpp` lines 851-853

**Problem:**
```cpp
InventoryItem selectedItem = inventory.getSelectedItem();
if (selectedItem.type == InventoryItemType::BLOCK) {
    if (selectedItem.blockID > 0) {
        glm::vec3 placePosition = target.blockPosition + target.hitNormal;
        world.placeBlock(placePosition, selectedItem.blockID, &renderer);  // blockID could be uninitialized
```

The check `blockID > 0` doesn't validate that blockID is <= registry.count()-1.

---

### 6. Vector Invalidation - mesh_buffer_pool (LOW-MEDIUM SEVERITY)
**Location:** `/home/user/voxel-engine/include/mesh_buffer_pool.h`

**Problem:** If multiple chunks generate meshes concurrently and use the same buffer pool, vector capacity could cause iterator invalidation. The code does acquire/release properly, but thread safety depends on correct locking at higher level (which is broken due to issue #1).

---

## Summary of Crash Causes

### Primary Cause: DEADLOCK (Issue #1)
Every block break/place operation deadlocks when trying to update affected chunks.

### Secondary Causes:
1. **Out-of-bounds registry access** when block IDs are invalid (corrupted data)
2. **Race conditions** if another thread modifies neighboring chunks during mesh generation
3. **Invalid block IDs** from inventory without bounds validation

---

## Recommended Fixes (Priority Order)

### 1. CRITICAL: Fix Deadlock
**File:** `src/world.cpp` and `src/chunk.cpp`

Change generateMesh() to use Unsafe versions when caller holds lock:
```cpp
// In chunk.cpp generateMesh() lambdas:
auto isSolid = [this, world, &registry, &localToWorldPos](int x, int y, int z) -> bool {
    int blockID;
    if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT && z >= 0 && z < DEPTH) {
        blockID = m_blocks[x][y][z];
    } else {
        // CHANGE: Use getBlockAtUnsafe or getChunkAtWorldPosUnsafe
        glm::vec3 worldPos = localToWorldPos(x, y, z);
        // This needs a new version that doesn't acquire locks
        // OR: Refactor to pass World reference with lock already held
        blockID = world->getBlockAtUnsafe(worldPos.x, worldPos.y, worldPos.z);  // <-- Use Unsafe
    }
    // ... rest of code
};
```

OR refactor to pass lock context:
```cpp
void Chunk::generateMesh(World* world, std::shared_lock<std::shared_mutex>& heldLock);
```

### 2. HIGH: Add Registry Bounds Checks
**File:** `src/chunk.cpp` line 493 and all other registry.get() calls

```cpp
int id = m_blocks[X][Y][Z];
if (id == 0) continue;

// Add bounds check BEFORE accessing registry
if (id < 0 || id >= registry.count()) {
    // Log warning and skip this block
    Logger::warning() << "Invalid block ID: " << id;
    continue;
}

const BlockDefinition& def = registry.get(id);
```

### 3. HIGH: Validate Block IDs from Inventory
**File:** `src/main.cpp` lines 851-853

```cpp
if (selectedItem.type == InventoryItemType::BLOCK) {
    if (selectedItem.blockID > 0 && selectedItem.blockID < registry.count()) {  // Add upper bound
        glm::vec3 placePosition = target.blockPosition + target.hitNormal;
        world.placeBlock(placePosition, selectedItem.blockID, &renderer);
    }
}
```

### 4. MEDIUM: Improve Lock Management
Consider:
- Making shared_mutex reentrant, OR
- Using a shared_lock throughout mesh generation, OR
- Refactoring to avoid nested locking

### 5. MEDIUM: Add Exception Handling
Wrap all registry.get() calls in try-catch (already done in player.cpp, but missing in chunk.cpp and elsewhere)

