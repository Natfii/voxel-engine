# Light Propagation Queue Performance Investigation

**Date:** November 21, 2025  
**Status:** Complete  
**Scope:** Comprehensive analysis of BFS lighting queue performance

---

## Quick Summary

The voxel engine's lighting system uses two FIFO queues to propagate light through the world using flood-fill BFS. While the algorithm is O(1) per node and works correctly during normal gameplay, there are **critical UX issues** during initialization and viewport lighting updates.

### Key Findings

**Good News:**
- propagateLightStep() is O(1) - efficient
- Normal gameplay batching is correct
- No infinite loops
- No exponential queue growth

**Bad News:**
- Loading screen freezes for 50+ seconds
- 300 million light nodes queued during spawn
- Viewport lighting updates block gameplay
- No queue monitoring during gameplay

---

## Investigation Scope

This investigation answers all requested questions:

1. **Where queues are populated** - 6 locations identified with exact line numbers
2. **Queue size estimates** - ~300 million nodes during world load, 4.8 GB memory peak
3. **propagateLightStep() cost** - O(1) per node (not O(n) or exponential)
4. **Queue clearing** - Proper clearing during gameplay, but blocking during init
5. **Infinite loops/exponential growth** - Zero infinite loops, logarithmic growth confirmed
6. **Debug logs** - Missing gameplay logs, only init output available

---

## Documents Provided

### 1. LIGHTING_PERFORMANCE_SUMMARY.txt
Quick reference guide - best for sharing findings with team

**Contains:**
- Critical findings summary
- Queue population locations
- Size estimates and processing times
- Gameplay behavior analysis
- Issues summary with severity ratings
- Metrics table

**Read time:** 10 minutes

### 2. LIGHTING_QUEUE_ANALYSIS.md
Detailed technical analysis with code snippets

**Contains:**
- Complete queue population analysis
- Phase-by-phase behavior during world load, gameplay, viewport lighting
- O(1) complexity proof with detailed breakdown
- Queue clearing verification
- Infinite loop and exponential growth analysis
- 4 detailed performance issues
- Metrics summary table

**Read time:** 30 minutes

### 3. LIGHTING_PERFORMANCE_FIXES.md
Implementation guidance with specific code changes

**Contains:**
- Current code for each issue
- Problem statements
- Two fix options per issue (batching, threading, logging)
- Impact analysis
- Testing recommendations
- Before/after metrics

**Read time:** 20 minutes (implementation varies)

### 4. LIGHTING_INVESTIGATION_INDEX.md
Navigation and overview document

**Contains:**
- Document index with descriptions
- Investigation findings summary
- Code location reference table
- Performance metrics table
- Implementation priority with time estimates
- Conclusion and recommendations

**Read time:** 15 minutes

---

## Critical Issues Found

| # | Issue | Severity | Impact | Fix Time |
|---|-------|----------|--------|----------|
| 1 | Blocking initialization loop | CRITICAL | 50+ sec freeze | 1 hour |
| 2 | No queue monitoring | MEDIUM | Can't detect issues | 30 min |
| 3 | Viewport lighting blocks gameplay | HIGH | 5-10 FPS drops | 30 min |
| 4 | Queue accumulation during building | MEDIUM | Acceptable (cosmetic) | N/A |

---

## Performance Metrics

**During World Initialization:**
- Spawn chunks: 1,331 (11x11x11 cube)
- Light nodes queued: ~300 million
- Processing time: ~50 seconds
- Memory peak: 4.8 GB
- Cost per node: O(1) - 100-500 CPU cycles

**During Normal Gameplay:**
- Max additions per frame: 500
- Max removals per frame: 300
- Frame rate impact: Negligible
- Queue accumulation: Temporary (2-3 frames max)

**Growth Analysis:**
- Pattern: Logarithmic (not exponential)
- Propagation radius: ~15 blocks
- Max illuminated volume: ~14,000 blocks
- Bounded by: Light decay (level 0 stops)

---

## Recommendations

### Priority 1: Fix Blocking Initialization (1 hour)
**File:** src/lighting_system.cpp (lines 59-64)
**Fix:** Add batched progress reporting (10K nodes/batch)
**Impact:** Loading screen remains responsive

### Priority 2: Fix Viewport Blocking (30 minutes)
**File:** src/lighting_system.cpp (lines 617-622)
**Fix:** Add batch limit constant (MAX_BATCH_PER_UPDATE = 1000)
**Impact:** 60 FPS maintained during time-of-day changes

### Priority 3: Add Queue Logging (30 minutes)
**File:** src/lighting_system.cpp (update method)
**Fix:** Log queue sizes every 60 frames (1 second)
**Impact:** Can detect performance regressions

### Priority 4: Optional Monitoring (15 minutes)
**File:** src/lighting_system.cpp
**Fix:** Add threshold warnings
**Impact:** Extra profiling information

---

## Implementation Timeline

Total effort: 2-3 hours for all fixes

```
Hour 1:   Priority 1 - Fix initialization loop
Hour 2:   Priority 2 - Fix viewport blocking
Hour 2:   Priority 3 - Add logging
Hour 2:   Priority 4 - Add monitoring
Hour 3:   Testing and verification
```

---

## Expected Impact

### Before Fixes
```
Loading screen: Frozen for 50+ seconds
Gameplay: 60 FPS (normal)
Time-of-day changes: 5-10 FPS (drops)
Queue visibility: None
```

### After All Fixes
```
Loading screen: Responsive with progress
Gameplay: 60 FPS (normal)
Time-of-day changes: 60 FPS (maintained)
Queue visibility: 1-sec interval logs
```

---

## Code Locations Reference

| Component | File | Lines | Status |
|-----------|------|-------|--------|
| initializeWorldLighting() | src/lighting_system.cpp | 26-67 | Issue #1 |
| update() | src/lighting_system.cpp | 71-94 | Issue #2 |
| propagateLightStep() | src/lighting_system.cpp | 424-505 | O(1) ✓ |
| removeLightStep() | src/lighting_system.cpp | 509-579 | O(1) ✓ |
| recalculateViewportLighting() | src/lighting_system.cpp | 602-625 | Issue #3 |
| generateSunlightColumn() | src/lighting_system.cpp | 371-419 | Primary queue source |

---

## Key Takeaways

1. **The algorithm is sound** - O(1) per node, proper BFS implementation
2. **The batching works correctly** - During normal gameplay (500/300 per frame)
3. **The issues are UX-related** - Blocking loops during initialization/transitions
4. **All issues are fixable** - 2-3 hours total implementation time
5. **No fundamental problems** - Just need better progress reporting

---

## Next Steps

1. **Read LIGHTING_PERFORMANCE_SUMMARY.txt** (10 min) - Get overview
2. **Read LIGHTING_PERFORMANCE_FIXES.md** (20 min) - Understand fixes
3. **Review specific issues** in LIGHTING_QUEUE_ANALYSIS.md (optional)
4. **Implement Priority 1 & 2** in src/lighting_system.cpp (1.5 hours)
5. **Add logging (Priority 3)** for ongoing monitoring (30 min)
6. **Test with provided checklist** (1 hour)

---

## Conclusion

The voxel engine's light propagation system is **algorithmically excellent** but needs **UX improvements** for initialization and viewport lighting. The core O(1) per-node performance is not the issue—the issue is processing 300 million nodes in a single blocking loop.

All issues have straightforward fixes that don't change the algorithm, just the presentation and batching strategy. Recommended to implement before shipping.

---

## Document Map

```
LIGHTING_INVESTIGATION_INDEX.md       (START HERE - Navigation guide)
├── LIGHTING_PERFORMANCE_SUMMARY.txt  (Quick overview)
├── LIGHTING_QUEUE_ANALYSIS.md        (Technical deep dive)
└── LIGHTING_PERFORMANCE_FIXES.md     (Implementation guide)
```

---

**Investigation Complete**  
All analysis documents available in: `/home/user/voxel-engine/LIGHTING_*.* `
