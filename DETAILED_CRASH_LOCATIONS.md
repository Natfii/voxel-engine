# Detailed Crash Location Reference

## ISSUE #1: DEADLOCK IN BLOCK BREAKING/PLACEMENT

### Root Cause Location
- **Primary:** `/home/user/voxel-engine/src/world.cpp` lines 818-928 (breakBlock function)
- **Secondary:** `/home/user/voxel-engine/src/world.cpp` lines 942-1011 (placeBlock function)
- **Tertiary:** `/home/user/voxel-engine/src/chunk.cpp` lines 453-483 (isSolid and isLiquid lambdas)

### Exact Problematic Code Sequence

**world.cpp - breakBlock (Line 818-928):**
```cpp
819: std::unique_lock<std::shared_mutex> lock(m_chunkMapMutex);  // ACQUIRES UNIQUE LOCK
...
882: Chunk* affectedChunk = getChunkAtWorldPosUnsafe(worldX, worldY, worldZ);
883: if (affectedChunk) {
884:     try {
885:         affectedChunk->generateMesh(this);  // CALLS GENERATEMESH WITH LOCK HELD
886:         affectedChunk->createVertexBuffer(renderer);
```

**chunk.cpp - generateMesh (Line 336-650+):**
```cpp
453: auto isSolid = [this, world, &registry, &localToWorldPos](int x, int y, int z) -> bool {
454:     int blockID;
455:     if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT && z >= 0 && z < DEPTH) {
456:         blockID = m_blocks[x][y][z];
457:     } else {
458:         // Out of bounds - check neighboring chunk via World
459:         glm::vec3 worldPos = localToWorldPos(x, y, z);
460:         blockID = world->getBlockAt(worldPos.x, worldPos.y, worldPos.z);  // TRIES TO LOCK AGAIN!
461:     }
```

**world.cpp - getBlockAt (Line 607-617):**
```cpp
607: int World::getBlockAt(float worldX, float worldY, float worldZ) {
608:     auto coords = worldToBlockCoords(worldX, worldY, worldZ);
609:     Chunk* chunk = getChunkAt(coords.chunkX, coords.chunkY, coords.chunkZ);
...

601: Chunk* World::getChunkAt(int chunkX, int chunkY, int chunkZ) {
602:     std::shared_lock<std::shared_mutex> lock(m_chunkMapMutex);  // TRIES TO LOCK MUTEX WE ALREADY OWN!
603:     return getChunkAtUnsafe(chunkX, chunkY, chunkZ);
604: }
```

### Deadlock Flow Diagram
```
Thread calls breakBlock()
    ↓
std::unique_lock acquired on m_chunkMapMutex (line 821)
    ↓
affectedChunk->generateMesh(this) called (line 885)
    ↓
Inside generateMesh, isSolid lambda needs neighboring chunk info
    ↓
isSolid calls world->getBlockAt() (line 461)
    ↓
getBlockAt calls getChunkAt() (line 611)
    ↓
getChunkAt tries std::shared_lock on m_chunkMapMutex (line 603)
    ↓
DEADLOCK! - Thread is trying to acquire ANOTHER lock on mutex it already owns uniquely
```

### Why This Crashes

1. **std::shared_mutex is NOT reentrant**
   - You cannot acquire a lock (shared or unique) on a mutex you already hold a lock on
   - Attempting to do so causes undefined behavior (deadlock or crash)

2. **Lock Hierarchy Violation**
   - Lock acquired at: line 821 (unique_lock)
   - Lock attempted at: line 603 (shared_lock on same mutex)
   - Recursive locking on non-reentrant mutex = DEADLOCK

3. **When It Occurs**
   - Every single block break or placement operation
   - Happens during mesh regeneration for neighboring chunks
   - Blocks the entire game thread

---

## ISSUE #2: OUT-OF-BOUNDS REGISTRY ACCESS

### Location and Examples

**chunk.cpp line 493 (NO BOUNDS CHECK):**
```cpp
489:     int id = m_blocks[X][Y][Z];
490:     if (id == 0) continue; // Skip air
491:
492:     // Look up block definition by ID
493:     const BlockDefinition& def = registry.get(id);  // CRASH if id >= registry.count()
```

**chunk.cpp lines 466, 482 (NO BOUNDS CHECK in lambdas):**
```cpp
453: auto isSolid = [this, world, &registry, &localToWorldPos](int x, int y, int z) -> bool {
...
463:     if (blockID == 0) return false;
464:     // Bounds check before registry access to prevent crash
465:     if (blockID < 0 || blockID >= registry.count()) return false;  // HAS CHECK ✓
466:     return !registry.get(blockID).isLiquid;
...

470: auto isLiquid = [this, world, &registry, &localToWorldPos](int x, int y, int z) -> bool {
...
479:     if (blockID == 0) return false;
480:     // Bounds check before registry access to prevent crash
481:     if (blockID < 0 || blockID >= registry.count()) return false;  // HAS CHECK ✓
482:     return registry.get(blockID).isLiquid;
```

Note: These lambdas DO have checks, but the main code at line 493 doesn't!

### Missing Bounds Checks

**water_simulation.cpp line 426:**
```cpp
425:     auto& registry = BlockRegistry::instance();
426:     const BlockDefinition& blockDef = registry.get(blockID);  // NO CHECK!
```

**water_simulation.cpp line 446:**
```cpp
445:     auto& registry = BlockRegistry::instance();
446:     const BlockDefinition& blockDef = registry.get(blockID);  // NO CHECK!
```

**world.cpp line 827:**
```cpp
824:     int blockID = getBlockAtUnsafe(worldX, worldY, worldZ);
...
827:     if (blockID != 0 && registry.get(blockID).isLiquid) {  // NO BOUNDS CHECK!
```

**world.cpp line 960:**
```cpp
960:     if (registry.get(blockID).isLiquid) {  // NO CHECK - blockID could be negative or too large!
```

### Why It Crashes

1. **Invalid Block ID Values**
   - Block IDs should be: 0 <= ID < registry.count()
   - ID could be corrupted in memory
   - Negative IDs cause out-of-bounds access
   - IDs > max throw exception in get()

2. **Undetected Corruption**
   - No validation of loaded/placed block IDs
   - Chunks could load corrupted data
   - Previous memory writes could corrupt block array

---

## ISSUE #3: RACE CONDITIONS IN CONCURRENT MESH GENERATION

### Location
- **Primary:** `/home/user/voxel-engine/src/chunk.cpp` lines 453-483
- **Secondary:** `/home/user/voxel-engine/src/world.cpp` lines 880-927

### The Race Scenario

**Scenario 1: Concurrent Break Operations**
```
Time 1: Thread A calls breakBlock at (10, 50, 20)
        -> Acquires unique_lock on m_chunkMapMutex
        -> Modifies block in chunk (0, 1, 0)
        -> Calls generateMesh for neighbors
        
Time 2: Thread B calls breakBlock at (20, 50, 20)  
        -> WAITS for lock (Thread A holds it)

Time 3: Thread A still in generateMesh
        -> Calls getBlockAt for neighbor (1, 1, 0)
        -> Gets shared_lock... DEADLOCKS instead!
```

**Scenario 2: Concurrent Modify Operations (if deadlock is fixed)**
```
Time 1: Thread A: breakBlock unique_lock acquired
        -> Starts reading blocks from 6 neighbors for mesh gen
        
Time 2: Thread B: placeBlock waiting for unique_lock
        -> Waiting...
        
Time 3: Thread A releases lock after mesh gen
        
Time 4: Thread B: placeBlock unique_lock acquired
        -> Modifies neighbor chunk that A just read from
        -> Calls generateMesh
        
Result: Data race - A might have used stale data from B's modification
```

---

## ISSUE #4: MISSING INVENTORY BLOCK ID VALIDATION

### Location
- `/home/user/voxel-engine/src/main.cpp` lines 846-854

**Code:**
```cpp
846:     // Get selected item from hotbar
847:     InventoryItem selectedItem = inventory.getSelectedItem();
848:
849:     if (selectedItem.type == InventoryItemType::BLOCK) {
850:         // Place block adjacent to the targeted block (using hit normal)
851:         if (selectedItem.blockID > 0) {  // ONLY CHECKS > 0, NOT < registry.count()!
852:             glm::vec3 placePosition = target.blockPosition + target.hitNormal;
853:             world.placeBlock(placePosition, selectedItem.blockID, &renderer);
854:         }
```

### Problem

1. **Incomplete Validation**
   - Only checks if blockID > 0
   - Doesn't validate blockID < registry.count()
   - If blockID = 999 but only 20 blocks registered → crash in registry.get()

2. **Uninitialized Data**
   - selectedItem could have garbage blockID value
   - No initialization checks in inventory

---

## SUMMARY TABLE

| Issue | Severity | File | Line(s) | Impact |
|-------|----------|------|---------|--------|
| Deadlock in breakBlock/placeBlock | CRITICAL | world.cpp | 818-928 | Every block break/place freezes game |
| Missing bounds check in generateMesh | HIGH | chunk.cpp | 493 | Corrupted block IDs crash mesh gen |
| Missing bounds checks throughout | HIGH | water_sim, world, block_sys | Multiple | Crashes on invalid block IDs |
| Missing bounds check in inventory | MEDIUM | main.cpp | 851 | Crash if inventory has bad block ID |
| Race conditions after deadlock fix | MEDIUM-HIGH | chunk.cpp, world.cpp | 453-927 | Data corruption if deadlock fixed alone |

