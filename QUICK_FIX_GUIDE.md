# Quick Fix Guide - Voxel Engine Block Placement Crashes

## The Problem in One Sentence
**Deadlock:** `breakBlock()` holds a unique lock, then calls `generateMesh()` which tries to acquire another lock on the same mutex, freezing the game.

## Critical Issues (Must Fix)

### Issue 1: Deadlock (CRITICAL)
- **Files:** `src/world.cpp` (lines 821, 945), `src/chunk.cpp` (lines 461, 477)
- **Symptom:** Game freezes every time you break/place a block
- **Fix:** Use `getBlockAtUnsafe()` inside `generateMesh()` since the caller already holds the lock

### Issue 2: No Block ID Validation (HIGH) 
- **Files:** `src/chunk.cpp` (line 493), `src/water_simulation.cpp` (lines 426, 446), etc.
- **Symptom:** Crash on corrupted block IDs
- **Fix:** Add `if (blockID >= registry.count()) return;` before `registry.get(blockID)`

### Issue 3: Incomplete Inventory Validation (MEDIUM)
- **File:** `src/main.cpp` (line 851)
- **Symptom:** Placing invalid block IDs crashes game
- **Fix:** Change `if (blockID > 0)` to `if (blockID > 0 && blockID < registry.count())`

## Quick Fix Checklist

- [ ] Fix deadlock in breakBlock/placeBlock
- [ ] Add bounds check in chunk.cpp line 493
- [ ] Add bounds check in water_simulation.cpp lines 426, 446
- [ ] Add bounds checks in world.cpp (multiple lines)
- [ ] Add bounds check in block_system.cpp (multiple lines)
- [ ] Add inventory validation in main.cpp line 851
- [ ] Test block breaking (10+ times)
- [ ] Test block placement (10+ times)
- [ ] Test at chunk boundaries
- [ ] Stress test: Break 1000 blocks
- [ ] Stress test: Place 1000 blocks

## Code Snippets for Fixes

### Fix 1: Deadlock - Change generateMesh calls
```cpp
// In world.cpp, instead of:
affectedChunk->generateMesh(this);

// Need to:
// Option A: Create unsafe version that doesn't lock
affectedChunk->generateMesh(this);  // Modify to use unsafe functions inside

// Option B: Pass lock reference
std::unique_lock<std::shared_mutex> lock(m_chunkMapMutex);
// ... modify generateMesh signature to accept lock
```

### Fix 2: Add Block ID Validation Pattern
```cpp
// BEFORE:
const BlockDefinition& def = registry.get(blockID);

// AFTER:
if (blockID < 0 || blockID >= registry.count()) {
    Logger::warning() << "Invalid block ID: " << blockID;
    continue; // or return appropriate default
}
const BlockDefinition& def = registry.get(blockID);
```

### Fix 3: Inventory Validation
```cpp
// BEFORE:
if (selectedItem.blockID > 0) {
    world.placeBlock(placePosition, selectedItem.blockID, &renderer);
}

// AFTER:
auto& registry = BlockRegistry::instance();
if (selectedItem.blockID > 0 && selectedItem.blockID < registry.count()) {
    world.placeBlock(placePosition, selectedItem.blockID, &renderer);
}
```

## Files That Need Bounds Checks

| File | Lines | Count |
|------|-------|-------|
| chunk.cpp | 493 | 1 |
| water_simulation.cpp | 426, 446 | 2 |
| world.cpp | 827, 865, 960, 1060, 1082, 1107, 1132, 1146 | 8 |
| block_system.cpp | 810, 835, 873 | 3 |
| inventory.cpp | 220, 322 | 2 |
| targeting_system.cpp | 176 | 1 |
| main.cpp | 851 | 1 (validation, not bounds check) |

**Total:** 18 locations need fixes

## Testing Script

```bash
# Build
cmake -B build && cmake --build build

# Test 1: Single block operations
echo "Test 1: Basic breaking/placement"
./voxel-engine  # Manually break 5 blocks, place 5 blocks

# Test 2: Rapid operations (requires automation)
# Break 100 blocks at same location
# Place 100 blocks at same location

# Test 3: Chunk boundaries
# Break blocks at chunk edges (e.g., at X=32, 64, 96)

# Test 4: Neighbor chunks
# Break block, check all 6 neighbors regenerate correctly
```

## Expected Results After Fixes

- Block breaking works instantly (no freeze)
- Block placement works instantly (no freeze)
- Mesh updates correctly for affected chunks
- No crashes from invalid block IDs
- Game runs smoothly during rapid operations

## If Still Crashing After Fixes

1. Check deadlock wasn't properly fixed - ensure no nested locking
2. Verify all bounds checks added - grep for `registry.get(`
3. Check for other places accessing block arrays out of bounds
4. Run with debugger to see exact crash location
5. Check for corrupted save files (reload test world)

## References

- Full analysis: `/tmp/crash_analysis.md`
- Detailed locations: `/tmp/detailed_crash_locations.md`
- Implementation summary: `/tmp/INVESTIGATION_SUMMARY.txt`
