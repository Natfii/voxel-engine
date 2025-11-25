# Performance Monitoring Guide

**Created:** 2025-11-24
**Purpose:** Debug performance issues as player moves away from spawn

## Overview

A comprehensive performance profiling system has been added to track frame timing, queue sizes, and identify bottlenecks in real-time.

## How to Use

### Enable Monitoring

```
debug perf
```

This toggles performance monitoring on/off. When enabled, reports print to console every 5 seconds (default).

### Change Report Interval

```
debug perf 10    # Report every 10 seconds
debug perf 30    # Report every 30 seconds
```

### What Gets Tracked

The performance monitor tracks:

1. **Frame Timing Breakdown:**
   - Input processing time
   - World streaming time
   - Decoration processing time
   - Chunk GPU upload time
   - Rendering time
   - Total frame time

2. **Queue Sizes:**
   - Pending decorations (chunks waiting for neighbors)
   - Decorations in progress (active background tasks)
   - Pending chunk loads (world streaming queue)
   - Completed chunks (ready for GPU upload)
   - Mesh generation queue (chunks waiting for meshing)

3. **Player Position:**
   - Current position
   - Distance from spawn point

## Performance Report Example

```
============================== Performance Report ==============================
Player: (256.4, 80.2, -128.6)
Distance from spawn: 312.5 blocks

--- Frame Timing (averaged over 300 frames) ---
FPS:            58.3 (14.2 - 22.8 ms)
Frame Time:     17.2 ms (100.0%)
  Input:        0.3 ms (1.7%)
  Streaming:    0.1 ms (0.6%)
  Decoration:   8.4 ms (48.8%)    <-- BOTTLENECK!
  Chunk Upload: 2.1 ms (12.2%)
  Render:       4.8 ms (27.9%)
  Unaccounted:  1.5 ms

--- Queue Sizes (current frame) ---
Pending Decorations:        45 (avg: 38)    <-- BACKING UP!
Decorations In Progress:    4 (avg: 4)
Pending Chunk Loads:        12 (avg: 15)
Completed Chunks:           3 (avg: 5)
Mesh Generation Queue:      2 (avg: 3)

--- Bottleneck Analysis ---
WARNING: High decoration backlog (45 chunks)
  - Consider increasing MAX_CONCURRENT_DECORATIONS
  - Consider processing more decorations per frame
WARNING: Decoration processing taking 8.4 ms/frame
  - This is a significant bottleneck
================================================================================
```

## Interpreting Results

### Identifying Bottlenecks

1. **High decoration time** - Decoration system is overloaded
   - Look at "Pending Decorations" queue size
   - If growing over time, decorations can't keep up with generation

2. **High chunk upload time** - GPU upload is slow
   - Look at "Completed Chunks" queue size
   - If growing, GPU is bottlenecked

3. **High streaming time** - Chunk loading/unloading is slow
   - Look at "Pending Chunk Loads" queue size
   - May indicate disk I/O or generation bottleneck

4. **Distance correlation** - Performance degrades with distance
   - Compare reports at different distances from spawn
   - Helps identify if issue is spatial (e.g., far chunks taking longer)

### Expected vs. Problem Values

**Good:**
- Frame time: 16-20 ms (50-60 FPS)
- Pending decorations: < 20 chunks
- Decorations in progress: 3-4 chunks
- Decoration time: < 2 ms/frame

**Warning:**
- Frame time: 20-33 ms (30-50 FPS)
- Pending decorations: 20-50 chunks
- Decoration time: 2-5 ms/frame

**Problem:**
- Frame time: > 33 ms (< 30 FPS)
- Pending decorations: > 50 chunks
- Decoration time: > 5 ms/frame

## Implementation Details

### Files Added

1. **include/perf_monitor.h** - Performance monitoring API
2. **src/perf_monitor.cpp** - Implementation
3. **src/main.cpp** - Integration in game loop
4. **src/console_commands.cpp** - Console command
5. **include/world.h** - Queue size getters
6. **include/world_streaming.h** - Mesh queue getter

### Performance Overhead

- Minimal: < 0.1 ms per frame when enabled
- No overhead when disabled
- Reports only print periodically (not every frame)

### Data Collection

The monitor collects data every frame:
- Timing: High-resolution clock (microsecond precision)
- Queue sizes: Direct query from subsystems
- History: Maintains rolling 600-frame window (10 seconds at 60 FPS)

## Optimization Opportunities Identified

Based on the profiling infrastructure, here are potential optimizations:

### 1. Decoration System Batching

**Current:** Processes 10 chunks/frame, 4 concurrent decorations
**Bottleneck:** Can back up to 45+ pending chunks when exploring new areas

**Optimization Options:**
- Increase MAX_CONCURRENT_DECORATIONS from 4 to 8
- Process more completed decorations per frame (4 → 8)
- Prioritize decorations closer to player
- Defer decorations for distant chunks

### 2. Async Mesh Generation Pool

**Current:** 4 persistent mesh worker threads
**Status:** Already optimized (was spawning 600+ threads/sec!)

**Further Options:**
- Scale thread count based on CPU cores
- Prioritize meshes closer to player
- Batch mesh generation for nearby chunks

### 3. GPU Upload Batching

**Current:** Batched uploads (10-15x reduction already)
**Status:** Already optimized

**Further Options:**
- Increase max uploads per frame if GPU has headroom
- Prioritize uploads for visible chunks
- Defer uploads for occluded chunks

### 4. Streaming Update Frequency

**Current:** Updates 4 times/second
**Status:** Already optimized (was 60 times/second)

**Further Options:**
- Dynamic frequency based on player velocity
- Skip updates when player is stationary
- Larger load/unload hysteresis based on distance from spawn

## Testing Workflow

1. **Start game and load world:**
   ```
   # In console (F9)
   debug perf 5
   ```

2. **Stay at spawn for 30 seconds:**
   - Collect baseline performance data
   - Should have low/stable queue sizes

3. **Move away from spawn in one direction:**
   - Note distance in reports
   - Watch for queue size growth
   - Track frame time changes

4. **Find performance cliff:**
   - Distance where FPS drops significantly
   - Note queue sizes at that point
   - Identify which system is bottlenecked

5. **Return to spawn:**
   - Check if performance recovers
   - Verify queues drain
   - Confirm issue is distance-related

## Next Steps

After identifying bottlenecks:

1. **Analyze specific system** - Focus profiling on the bottlenecked system
2. **Experiment with parameters** - Adjust constants (thread counts, batch sizes)
3. **Profile algorithms** - Add fine-grained timing within the bottleneck
4. **Implement optimizations** - Apply batching, async, or algorithm improvements
5. **Measure improvement** - Use perf monitor to verify gains

## Troubleshooting

**"Reports not printing"**
- Check that monitoring is enabled: `debug perf`
- Reports only print to stdout (console window, not in-game)
- May need to wait for report interval to elapse

**"Queue sizes always zero"**
- World may be fully loaded (no streaming happening)
- Move to new areas to trigger chunk generation
- Check that world streaming is active

**"Frame times inconsistent"**
- Reports show averages over recent history
- Ignore first report (not enough data)
- Look for trends over multiple reports

## Code Integration Points

### Main Loop Integration (src/main.cpp:833-1414)

```cpp
// Frame boundary
PerformanceMonitor::instance().beginFrame();

// ... game systems run here ...

// Record timings and queue sizes
PerformanceMonitor::instance().recordTiming("decoration", ms);
PerformanceMonitor::instance().recordQueueSize("pending_decorations", count);
PerformanceMonitor::instance().recordPlayerPosition(pos, spawnPos);

// End frame (triggers report if interval elapsed)
PerformanceMonitor::instance().endFrame();
```

### Scoped Timing (Alternative API)

```cpp
{
    PERF_SCOPE("my_system");
    // ... code to measure ...
}  // Automatically records timing
```

---

**For more details, see:**
- `include/perf_monitor.h` - API documentation
- `src/perf_monitor.cpp` - Implementation
- ENGINE_HANDBOOK.md - Overall architecture
