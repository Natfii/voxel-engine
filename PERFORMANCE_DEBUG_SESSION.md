# Performance Debug Session - 2025-11-24

## Issue Report
User reported performance tanks as they move away from spawn. Standing at spawn shows:
- **120 pending decorations (stuck!)**
- **0 decorations in progress (nothing processing!)**
- **Periodic 77-113ms GPU fence stalls**

## Diagnostics Added

### 1. Performance Monitoring System
**Files:** `include/perf_monitor.h`, `src/perf_monitor.cpp`

Comprehensive frame timing and queue tracking:
- Frame time breakdown (input, streaming, decoration, chunk upload, render)
- Queue sizes (pending decorations, decorations in progress, pending loads, completed chunks, mesh queue)
- Distance from spawn tracking
- Automatic bottleneck analysis
- Reports every 5 seconds

**Usage:** `debug perf` in console (F9)

### 2. Decoration System Diagnostics
**File:** `src/world.cpp:686-694`

Added detailed logging to identify why 120 chunks are stuck:

```cpp
[DECORATION DIAGNOSTIC] Pending: 120 | Slots: 4 | Blocked by neighbors: X | Already decorated: Y | Ready to process: Z
```

**Possible causes:**
- Blocked by neighbors → `hasHorizontalNeighbors()` failing (circular dependency?)
- Already decorated → Cleanup bug, chunks not removed from queue
- Ready to process = 0 → Something blocking launch

### 3. GPU Fence Stall Diagnostics
**File:** `src/vulkan_renderer.cpp:1266-1297`

Tracks fence wait times and logs slow waits (>10ms):

```cpp
[GPU FENCE STALL] 77ms wait | Pending async uploads: X | Deletion queue: Y
```

**Possible causes:**
- High pending uploads → GPU can't keep up with chunk upload rate
- High deletion queue → Buffer cleanup backing up

## Performance Report Example

```
============================== Performance Report ==============================
Player: (0.00, 96.60, 0.00)
Distance from spawn: 0.90 blocks

--- Frame Timing (averaged over 600 frames) ---
FPS:            849.40 (0.08 - 88.06 ms)
Frame Time:     1.18 ms (100.0%)
  Input:        0.07 ms (5.93%)
  Streaming:    0.03 ms (2.79%)
  Decoration:   0.03 ms (2.38%)
  Chunk Upload: 0.00 ms (0.02%)
  Render:       1.07 ms (91.09%)

--- Queue Sizes (current frame) ---
Pending Decorations:        120 (avg: 120)    ⚠️ STUCK!
Decorations In Progress:    0 (avg: 0)        ⚠️ NOTHING PROCESSING!
Pending Chunk Loads:        0 (avg: 0)
Completed Chunks:           0 (avg: 0)
Mesh Generation Queue:      0 (avg: 0)

--- Bottleneck Analysis ---
WARNING: High decoration backlog (120 chunks)
  - Consider increasing MAX_CONCURRENT_DECORATIONS
  - Consider processing more decorations per frame
================================================================================
```

## Findings

### Issue #1: GPU Fence Stalls (77-113ms periodic)
**Symptom:** `beginFrame=77-113ms` in slow frame logs

**Analysis:**
- Normal frames: 1.18ms (849 FPS) - Excellent!
- Slow frames: 77-113ms periodic spikes - Bad!
- Pattern: GPU periodically gets backed up, then catches up

**Root Cause (Suspected):**
- Async upload fence backlog
- `processAsyncUploads()` limit of 30 completions/frame may not be enough
- V-sync timing causing periodic waits

**Next Steps:**
1. Check `[GPU FENCE STALL]` output to see pending uploads count
2. If pending uploads > 20 consistently, increase `MAX_UPLOAD_COMPLETIONS_PER_FRAME`
3. Consider disabling V-sync to test (may be waiting for monitor refresh)

### Issue #2: Decoration System Completely Frozen
**Symptom:** 120 pending, 0 in progress

**Analysis:**
- Decorations stuck in `m_pendingDecorations` set
- `processPendingDecorations()` called every frame but not launching tasks
- Either `hasHorizontalNeighbors()` failing OR chunks don't actually need decoration

**Root Cause (Suspected):**
1. **Most likely:** `hasHorizontalNeighbors()` returns false for all chunks
   - Checks if neighbors are `isTerrainReady()`
   - At spawn, all chunks should be ready, so this is likely a bug
   - Possible: neighbor chunks exist but `isTerrainReady()` flag not set

2. **Less likely:** Chunks already decorated but not removed from queue
   - Would show "Already decorated: 120" in diagnostic

**Next Steps:**
1. Check `[DECORATION DIAGNOSTIC]` output to see exact reason
2. If "Blocked by neighbors: 120":
   - Bug in `hasHorizontalNeighbors()` or `isTerrainReady()`
   - Need to investigate why neighbors aren't marked ready
3. If "Already decorated: 120":
   - Cleanup bug in decoration completion logic
   - Need to ensure chunks removed from `m_pendingDecorations` after decoration

## Optimization Opportunities Identified

### 1. Decoration System
**Current:**
- 10 chunks/frame processed
- 4 concurrent decoration tasks max
- Async decoration (good!)

**Optimizations:**
- **Increase concurrency:** `MAX_CONCURRENT_DECORATIONS` 4→8 threads
- **Increase processing:** `MAX_COMPLETED_PER_FRAME` 4→8 chunks/frame
- **Add prioritization:** Process chunks closer to player first
- **Fix deadlock:** Once diagnostic reveals issue, fix the root cause

### 2. GPU Upload System
**Current:**
- Already batched (good!)
- Async uploads (good!)
- 30 completions/frame limit

**Optimizations:**
- **Dynamic limits:** Adjust based on GPU capacity
- **Prioritization:** Upload visible chunks first
- **Defer distant chunks:** Don't upload chunks beyond render distance immediately

### 3. Mesh Generation
**Current:**
- 4 persistent worker threads (good!)
- Already optimized (eliminated 600+ thread spawns/sec)

**Optimizations:**
- **Scale threads:** Use more threads on high-core CPUs
- **Prioritization:** Mesh chunks closer to player first

## Testing Protocol

1. **Build and run:**
   ```cmd
   build.bat
   run.bat
   ```

2. **Enable diagnostics:**
   ```
   F9                    # Open console
   debug perf            # Enable performance monitoring
   ```

3. **Observe at spawn (standing still):**
   - Wait 10-15 seconds for diagnostic messages
   - Look for `[DECORATION DIAGNOSTIC]` every 5 seconds
   - Look for `[GPU FENCE STALL]` when slow frames occur
   - Check performance reports every 5 seconds

4. **Record observations:**
   - Decoration diagnostic values (blocked by neighbors, already decorated, etc.)
   - GPU fence stall patterns (pending uploads, deletion queue size)
   - Performance report trends

5. **Test while moving:**
   - Move 100 blocks away from spawn
   - Move 200 blocks away
   - Move 300+ blocks away
   - Note when performance degrades
   - Check if decoration backlog grows

## Expected Diagnostic Output

### Scenario A: Neighbor Check Bug
```
[DECORATION DIAGNOSTIC] Pending: 120 | Slots: 4 | Blocked by neighbors: 120 | Already decorated: 0 | Ready to process: 0
```
**Action:** Fix `hasHorizontalNeighbors()` or `isTerrainReady()` logic

### Scenario B: Cleanup Bug
```
[DECORATION DIAGNOSTIC] Pending: 120 | Slots: 4 | Blocked by neighbors: 0 | Already decorated: 120 | Ready to process: 0
```
**Action:** Fix decoration completion to remove from pending queue

### Scenario C: GPU Upload Backlog
```
[GPU FENCE STALL] 77ms wait | Pending async uploads: 45 | Deletion queue: 15
```
**Action:** Increase `MAX_UPLOAD_COMPLETIONS_PER_FRAME` or reduce upload rate

### Scenario D: Buffer Deletion Backlog
```
[GPU FENCE STALL] 113ms wait | Pending async uploads: 5 | Deletion queue: 200
```
**Action:** Increase `MAX_DELETIONS_PER_FRAME` or defer deletions

## Files Modified

### Performance Monitoring
- `include/perf_monitor.h` - Performance monitor API
- `src/perf_monitor.cpp` - Implementation
- `src/main.cpp` - Integration in game loop
- `src/console_commands.cpp` - Console command (`debug perf`)
- `include/world.h` - Queue size getters
- `include/world_streaming.h` - Mesh queue getter

### Diagnostics
- `src/world.cpp:686-694` - Decoration diagnostic output
- `src/vulkan_renderer.cpp:1266-1297` - GPU fence diagnostic output

### Documentation
- `PERF_MONITORING_GUIDE.md` - Complete usage guide
- `PERFORMANCE_DEBUG_SESSION.md` - This file

## Next Steps After Running Diagnostics

1. **Analyze output** - Identify exact cause from diagnostic messages
2. **Fix root cause** - Address the specific issue revealed
3. **Implement optimizations** - Apply batching/async improvements
4. **Measure improvement** - Use perf monitor to verify gains
5. **Test at distance** - Verify performance doesn't degrade away from spawn

---

**Status:** Awaiting diagnostic output from test run
**Date:** 2025-11-24
**Session:** Active debugging of decoration freeze and GPU stalls
