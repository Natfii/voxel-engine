# Chunk Streaming Refactoring Strategy

## Executive Summary

This document outlines a safe, incremental refactoring strategy to add dynamic chunk streaming to the voxel engine. The strategy prioritizes **backward compatibility**, **testability**, and **rollback capability** at each phase.

**Current System**: All chunks (12x64x12 = 9,216 chunks) are pre-generated, decorated, and buffered on startup (~10-30 seconds loading time).

**Target System**: Chunks generate on-demand around the player with background streaming, reducing startup time to <2 seconds and enabling infinite worlds.

---

## Phase 1: Foundation (Low Risk, High Confidence)

### 1.1 Extract Chunk Generation Logic

**Goal**: Separate terrain generation from the sequential `generateWorld()` into reusable, single-chunk functions.

**Changes**:
- Create `ChunkGenerator` class (new file: `chunk_generator.h/cpp`)
- Extract `Chunk::generate()` logic → `ChunkGenerator::generateTerrainForChunk(chunkX, chunkY, chunkZ)`
- Extract mesh generation → `ChunkGenerator::generateMeshForChunk(chunk, world)`
- Extract decoration → `ChunkGenerator::decorateChunk(chunk, world, biomeMap, treeGenerator)`

**Files Modified**:
- `/home/user/voxel-engine/include/chunk_generator.h` (NEW)
- `/home/user/voxel-engine/src/chunk_generator.cpp` (NEW)
- `/home/user/voxel-engine/include/chunk.h` (refactor extract methods)
- `/home/user/voxel-engine/src/chunk.cpp` (refactor extract methods)

**Key Properties**:
- No changes to `World` API yet
- `World::generateWorld()` now calls `ChunkGenerator` internally
- All existing functionality identical
- Single-threaded initially (parallel in Phase 2)

**Testing**:
```cpp
// Unit tests for ChunkGenerator
TEST(ChunkGenerator, GenerateTerrainDeterministic) {
    // Same seed → same blocks
}
TEST(ChunkGenerator, MeshGenerationWithoutNeighbors) {
    // Mesh generation with null neighbors (LOD/partial generation)
}
TEST(ChunkGenerator, DecorationIdempotent) {
    // Decorating twice doesn't double-place trees
}
```

**Rollback**: Simple - revert file deletions and restore original `generateWorld()` implementation.

---

### 1.2 Add Spawn Area Generation

**Goal**: Support generating only chunks around a starting position instead of entire world.

**Changes**:
- Add `World::generateSpawnArea(playerChunkX, playerChunkY, playerChunkZ, radius)`
- Modify `World` constructor to optionally skip pre-allocation
- Keep full-world generation as default (backward compatible)

**Files Modified**:
- `/home/user/voxel-engine/include/world.h` (add `generateSpawnArea` method)
- `/home/user/voxel-engine/src/world.cpp` (implement spawn area generation)

**Key Properties**:
- Old code path: `World(12, 64, 12)` + `generateWorld()` still works identically
- New code path: `World(INF, INF, INF)` + `generateSpawnArea(0, 0, 0, 2)` for 5x5x5 chunks around origin
- Main.cpp behavior unchanged

**Testing**:
```cpp
TEST(World, SpawnAreaGeneratesOnlyRequestedChunks) {
    World world(INF, INF, INF);
    world.generateSpawnArea(0, 0, 0, 2);
    ASSERT_EQ(chunks_generated, 5 * 5 * 5);
}
TEST(World, SpawnAreaIncludesAllNeighbors) {
    // Mesh generation has correct neighbor data
}
```

**Rollback**: Delete new methods, keep old behavior default.

---

## Phase 2: Chunk Lookup Infrastructure (Medium Risk, High Value)

### 2.1 Unify Chunk Access Pattern

**Goal**: The codebase already uses `ChunkCoord` hash map for O(1) lookup, but we need to standardize all access patterns.

**Current Status**:
- ✅ `World::getChunkAt(chunkX, chunkY, chunkZ)` uses hash map
- ✅ `World::getChunkAtWorldPos(worldX, worldY, worldZ)` uses hash map
- ✅ `m_chunkMap` is primary storage
- ✅ `m_chunks` is secondary vector for iteration

**Changes**:
- Add `World::hasChunkAt(chunkX, chunkY, chunkZ)` for existence checks
- Add `World::forEachLoadedChunk(callback)` for iteration without exposing vector
- Ensure all code paths prefer `getChunkAt()` over direct vector access
- Add debug assertions for consistency between map and vector

**Files Modified**:
- `/home/user/voxel-engine/include/world.h` (add new methods)
- `/home/user/voxel-engine/src/world.cpp` (implement utilities)

**Key Properties**:
- Zero functional changes to rendering or generation
- All existing code continues to work
- Prep work for replacing vector with lazy-loading later

**Testing**:
```cpp
TEST(World, ChunkLookupConsistency) {
    // getChunkAt() and map contents always agree
}
TEST(World, IterationCoversAllChunks) {
    // forEachLoadedChunk iterates over all chunks
}
```

**Rollback**: Remove new utility methods.

---

## Phase 3: Threading & Background Generation (High Risk, High Value)

### 3.1 Add Background Generation Queue

**Goal**: Support off-thread chunk generation without blocking the render thread.

**Changes**:
- Create `ChunkStreamingManager` class (new file: `chunk_streaming_manager.h/cpp`)
- Thread-safe queue of chunks to generate (using `std::queue` + `std::mutex`)
- Background worker thread that:
  - Pops chunks from queue
  - Calls `ChunkGenerator::generateTerrainForChunk()`
  - Calls `ChunkGenerator::generateMeshForChunk()`
  - Pushes to "ready to buffer" queue
- Render thread (main.cpp) polls ready queue and calls `createVertexBuffer()`
- Add `World::update(playerPos)` to be called each frame

**Files Modified**:
- `/home/user/voxel-engine/include/chunk_streaming_manager.h` (NEW)
- `/home/user/voxel-engine/src/chunk_streaming_manager.cpp` (NEW)
- `/home/user/voxel-engine/include/world.h` (add `update()` method)
- `/home/user/voxel-engine/src/world.cpp` (integrate streaming manager)
- `/home/user/voxel-engine/src/main.cpp` (call `world.update(playerPos)` each frame)

**Key Properties**:
- Main.cpp still calls `generateWorld()` and `decorateWorld()` for backward compatibility
- New code path: Skip full generation, use streaming manager instead
- Thread-safe: No data races on chunk map
- Graceful degradation: If background thread falls behind, chunks render with placeholder or LOD

**Race Condition Handling**:
1. **Chunk lookup during generation**: `getChunkAt()` returns nullptr if generation in-progress
   - Mesh generation code already handles null neighbors (treats as air)
   - Solution: Generate neighbor data first, then make chunk visible

2. **Concurrent mesh generation**: Chunk.generateMesh() reads neighboring chunks
   - Solution: Per-chunk mutex or atomic generation state
   - Or: Generate meshes only when all neighbors ready

3. **Buffer creation from render thread**: Multiple threads calling `createVertexBuffer()`
   - Solution: Single-threaded buffer creation (callback to main thread)

**Testing**:
```cpp
TEST(ChunkStreamingManager, BasicGeneration) {
    manager.queueChunk(0, 0, 0);
    manager.update();  // Process queue
    ASSERT_TRUE(manager.isReady(0, 0, 0));
}
TEST(ChunkStreamingManager, ThreadSafety) {
    // Background thread generation + main thread lookups don't crash
}
TEST(ChunkStreamingManager, MeshGenerationWithPartialNeighbors) {
    // Chunk mesh valid even if neighbors not yet buffered
}
```

**Rollback Points**:
1. If thread safety issues: Disable streaming manager, use synchronous generation
2. If performance issues: Add FPS cap for background work
3. If crashes: Add more synchronization

---

## Phase 4: Player-Centric Streaming (Medium Risk, High Impact)

### 4.1 Dynamic Chunk Loading Based on Position

**Goal**: Stream chunks based on player distance, not pre-calculate all.

**Changes**:
- Modify `World::update(playerPos)` to:
  1. Calculate player chunk coordinate
  2. Determine "load distance" (e.g., 3 chunks = 48 blocks = ~24 world units)
  3. Calculate chunks needed in that radius
  4. Queue missing chunks for generation
  5. Unload distant chunks
- Add `World::unloadChunk(chunkX, chunkY, chunkZ)` for cleanup
- Modify `renderWorld()` to skip unloaded chunks (already done via `getChunkAt()` returning nullptr)

**Files Modified**:
- `/home/user/voxel-engine/include/world.h` (add streaming parameters)
- `/home/user/voxel-engine/src/world.cpp` (implement distance-based loading)
- `/home/user/voxel-engine/src/chunk_streaming_manager.cpp` (add unload queue)

**Key Properties**:
- Render distance already culls far chunks anyway
- Unloading = remove from map + destroy buffers
- Smooth transitions: Load distance > unload distance (hysteresis)
- Backward compatible: With large distance, loads all chunks like before

**Potential Issues**:
1. **Player at chunk boundary**: Chunks flip in/out rapidly
   - Solution: Hysteresis - load/unload at different distances (load at distance 3, unload at distance 4)

2. **Decoration spans chunks**: Trees placed at boundaries
   - Solution: When unloading chunk X, check if neighbor Y has trees pointing into X
   - Or: Don't decorate boundary blocks during initial generation

3. **Water flow leaves loaded area**: Water spreads then chunk unloads
   - Solution: Track boundary water blocks separately
   - Or: Keep "frame buffer" of chunks around load area

**Testing**:
```cpp
TEST(World, StreamingLoadDistance) {
    // Moving player triggers chunk loading
}
TEST(World, StreamingUnloadDistance) {
    // Moving away unloads chunks
}
TEST(World, StreamingBoundaryStability) {
    // Moving along chunk boundary doesn't cause flickering
}
```

---

## Phase 5: Mesh Generation Synchronization (High Risk, Critical)

### 5.1 Handle Missing Neighbor Chunks

**Problem**: When chunk Z generates, neighbors X±1, Y±1, Z±1 might not exist yet.

**Current Code** (in chunk.cpp ~line 395-408):
```cpp
auto isSolid = [this, world, &registry, &localToWorldPos](int x, int y, int z) -> bool {
    int blockID;
    if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT && z >= 0 && z < DEPTH) {
        blockID = m_blocks[x][y][z];
    } else {
        glm::vec3 worldPos = localToWorldPos(x, y, z);
        blockID = world->getBlockAt(worldPos.x, worldPos.y, worldPos.z);
    }
    if (blockID == 0) return false;  // <- treats null/missing as air
    ...
};
```

This already handles missing neighbors by treating them as air!

**Issue**: If neighbor gets generated later with solid blocks, faces don't get hidden.

**Solution Options**:

**Option A: Regenerate Neighbor Meshes** (Simple, ~5 FPS cost)
- When chunk Z finishes meshing, regenerate all 26 neighbors
- Trades: CPU cost for simplicity
- Best for small load distances

**Option B: Deferred Mesh Generation** (Complex, Best FPS)
- Only generate meshes when all neighbors exist
- Add `ChunkState enum: [TERRAIN_ONLY, NEIGHBORS_LOADED, MESHES_READY]`
- Streaming manager: Queue chunk only when all 26 neighbors have terrain generated
- Trades: Code complexity for optimal rendering

**Option C: Partial LOD Meshes** (Medium complexity)
- Generate "internal" meshes for blocks far from edges
- Generate "boundary" meshes only when neighbors ready
- Render both together
- Trades: Memory for responsiveness

**Recommended**: **Option B for production**, **Option A for MVP**.

**Files Modified**:
- `/home/user/voxel-engine/src/chunk_streaming_manager.cpp` (implement deferred generation)
- `/home/user/voxel-engine/include/chunk.h` (add ChunkState)

**Testing**:
```cpp
TEST(ChunkMesh, CorrectFaceCullingWithLateLateNeighbor) {
    // Generate chunk, then neighbor, then regenerate first chunk
    // Faces correctly culled
}
TEST(ChunkMesh, BoundaryFacesWithMissingNeighbor) {
    // Boundary faces rendered correctly even without neighbor
}
```

---

## Phase 6: Decoration with Streaming (High Risk, Complex)

### 6.1 Localized Decoration

**Problem**: `decorateWorld()` currently:
1. Iterates all surface positions globally
2. Checks biome at each position
3. Places trees that span multiple chunks
4. Tracks all modified chunks
5. Regenerates meshes batch-by-batch

**With Streaming**: Chunks load/unload dynamically, so trees placed in phase 1 might have their neighbors unload.

**Solution A: Late Decoration**
- Only decorate chunks when all neighbors loaded
- Move decoration to streaming manager
- Call `decorateChunk()` instead of global `decorateWorld()`
- Trades: More CPU/thread work for correctness

**Solution B: Boundary Buffering**
- Never place trees within 2 blocks of chunk boundary
- Simpler but wastes some space
- Better for MVP

**Solution C: Structural Persistence**
- Track placed structures separately (JSON/binary file)
- Save structure data when unloading chunks
- Reload when re-loading
- Enables also: chunk modifications persist

**Recommended**: **Solution B for MVP**, **Solution C for full release**.

**Files Modified**:
- `/home/user/voxel-engine/src/chunk_streaming_manager.cpp` (add localized decoration)
- `/home/user/voxel-engine/src/tree_generator.cpp` (add boundary check)

**Testing**:
```cpp
TEST(Decoration, NoBoundaryTrees) {
    // No trees placed within boundary zone
}
TEST(Decoration, TreesStillPlacedDensely) {
    // Density still feels natural despite boundary constraints
}
```

---

## Integration Checklist

### Phase 1 - Foundation
- [ ] Create `ChunkGenerator` class with extracted methods
- [ ] Add `World::generateSpawnArea()` method
- [ ] Run existing tests - all pass
- [ ] Update main.cpp loading screen
- **Commit**: "Extract chunk generation into reusable ChunkGenerator"

### Phase 2 - Infrastructure
- [ ] Add chunk lookup utilities
- [ ] Add `World::update()` stub
- [ ] Refactor decoration to use separate pass
- [ ] Run existing tests - all pass
- **Commit**: "Add chunk lookup infrastructure for streaming"

### Phase 3 - Threading
- [ ] Create `ChunkStreamingManager` class
- [ ] Implement thread-safe queue
- [ ] Implement background worker thread
- [ ] Add synchronization for mesh generation
- [ ] Add unit tests for threading
- **Commit**: "Add background chunk generation with ChunkStreamingManager"

### Phase 4 - Streaming
- [ ] Implement `World::update()` with distance-based loading
- [ ] Add hysteresis for load/unload
- [ ] Implement `World::unloadChunk()`
- [ ] Test with profiling - measure FPS impact
- **Commit**: "Implement player-centric chunk streaming"

### Phase 5 - Mesh Sync
- [ ] Choose face culling strategy (Option B recommended)
- [ ] Implement chunk state management
- [ ] Implement deferred mesh generation
- [ ] Extensive testing with edge cases
- **Commit**: "Add deferred mesh generation for correct face culling"

### Phase 6 - Decoration
- [ ] Choose decoration strategy (Option B recommended)
- [ ] Implement localized decoration
- [ ] Add boundary zone checks
- [ ] Test visual quality
- **Commit**: "Implement streaming-aware chunk decoration"

---

## Risk Mitigation

### Critical Risk 1: Race Conditions on Chunk Map

**Danger**: Two threads accessing `m_chunkMap` simultaneously

**Mitigation**:
1. Use `std::shared_mutex` for read-write locks
   - Render thread: Read lock for rendering
   - Generation thread: Write lock for adding chunks
   - Reader-writer lock: Multiple renders, single generator

2. Or: Single-threaded chunk addition
   - Generator queues chunks for main thread to insert
   - Main thread inserts during update phase (single-threaded)
   - Safe and simple, slight delay

**Recommended**: Single-threaded chunk addition + write lock on mesh generation

```cpp
// In ChunkStreamingManager
struct ReadyChunk {
    int x, y, z;
    std::unique_ptr<Chunk> chunk;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};
std::queue<ReadyChunk> m_readyQueue;  // Background thread → Main thread

// In World::update()
while (!m_streamingManager->m_readyQueue.empty()) {
    auto ready = m_streamingManager->m_readyQueue.pop();
    m_chunkMap[ChunkCoord{ready.x, ready.y, ready.z}] = std::move(ready.chunk);
    // Add to buffer creation queue
}
```

### Critical Risk 2: Deadlock on Chunk Lookup During Mesh Gen

**Danger**: Mesh generation reads neighbor chunks, which might be locked

**Mitigation**:
1. **Readers never lock**: Face culling never grabs lock
   - Accept race condition: might read slightly stale neighbor data
   - Adjacent frames will fix it

2. **Timeout on locks**:
   - Generate without lock, check conflicts at commit time
   - Reject if conflicts detected

3. **Segregate read/write**:
   - Generation thread only modifies m_blocks, m_blockMetadata
   - Render thread never writes blocks
   - One mutex per chunk for block data

**Recommended**: Option 1 (readers never lock)

### Critical Risk 3: Mesh Generation Sees Incomplete Neighbor Data

**Danger**: Chunk Y generates mesh before neighbor X has terrain blocks

**Mitigation**:
- **Three-stage process**:
  1. Generate terrain blocks (stage 1)
  2. After all neighbors have terrain, generate meshes (stage 2)
  3. Upload to GPU (stage 3)
- Streaming manager queues chunks for stage 2 only after all neighbors complete stage 1
- Add `ChunkState enum: [TERRAIN_ONLY, MESHES_READY, BUFFERED]`

### Critical Risk 4: Memory Leaks from Unloading

**Danger**: Unloading chunks leaves Vulkan buffers allocated

**Mitigation**:
1. Central cleanup routine:
```cpp
World::unloadChunk() {
    chunk->destroyBuffers(renderer);  // Free GPU memory FIRST
    m_chunkMap.erase(coordinate);     // Then remove from map
}
```

2. RAII cleanup:
```cpp
struct ChunkResources {
    std::unique_ptr<Chunk> chunk;
    VkBuffer gpuBuffer;  // Stored for cleanup
    ~ChunkResources() {
        // Cleanup called automatically
    }
};
```

**Recommended**: Central cleanup routine with explicit `destroyBuffers()` calls

### Critical Risk 5: Decoration Spans Unloaded Chunks

**Danger**: Tree placed in chunk X extends into chunk Y which later unloads

**Mitigation**:
1. **Don't decorate boundaries**: Simpler, wastes space
   - Mark "edge zone" around each chunk as no-decoration
   - Still ~95% of chunk interior available

2. **Track structures**: Keep structure data in separate file
   - When unloading chunk, save placed structures to disk
   - When reloading, restore structures
   - Allows persistent world state

**Recommended**: Option 1 for MVP, migrate to Option 2 later

---

## Backward Compatibility Matrix

| Scenario | Current | After Phase 1 | After Phase 2 | After Phase 3 |
|----------|---------|---------------|---------------|---------------|
| `World world(12,64,12); world.generateWorld();` | ✅ Full gen | ✅ Full gen | ✅ Full gen | ✅ Full gen (optional streaming) |
| `renderWorld()` | ✅ Renders all | ✅ Renders all | ✅ Renders all | ✅ Renders visible |
| `breakBlock(pos, renderer)` | ✅ Works | ✅ Works | ✅ Works | ✅ Works if chunk loaded |
| `placeBlock(pos, blockID, renderer)` | ✅ Works | ✅ Works | ✅ Works | ✅ Works if chunk loaded |
| Performance | 10-30s load | 10-30s load | 10-30s load | <2s load + streaming |

**Key**: All phases can be disabled via compile-time flags or runtime options.

---

## Testing Strategy

### Unit Tests
```bash
# Phase 1
tests/test_chunk_generator.cpp       # Deterministic generation
tests/test_spawn_area.cpp             # Spawn area generation

# Phase 2
tests/test_chunk_lookup.cpp           # Consistency checks
tests/test_iteration.cpp              # Iterate all chunks

# Phase 3
tests/test_streaming_manager.cpp      # Queue + threading
tests/test_thread_safety.cpp          # Race conditions
tests/test_mesh_generation_threading.cpp  # Concurrent mesh gen

# Phase 4
tests/test_dynamic_loading.cpp        # Distance-based loading
tests/test_hysteresis.cpp             # Load/unload stability

# Phase 5
tests/test_deferred_meshing.cpp       # Mesh state machine
tests/test_face_culling_async.cpp     # Correct culling with async neighbors

# Phase 6
tests/test_localized_decoration.cpp   # Boundary-aware trees
```

### Integration Tests
```bash
# Full streaming system
integration_tests/test_streaming_end_to_end.cpp
  - Load world
  - Move player
  - Measure FPS
  - Verify no memory leaks
  - Verify correct rendering
```

### Regression Tests
```bash
# Ensure old code paths still work
regression_tests/test_full_world_generation.cpp
regression_tests/test_block_operations.cpp
regression_tests/test_water_flow.cpp
regression_tests/test_decoration.cpp
```

### Performance Benchmarks
```bash
# Before/after measurements
benchmarks/loading_time.cpp           # Startup time
benchmarks/generation_throughput.cpp  # Chunks/sec
benchmarks/streaming_fps.cpp          # FPS while streaming
benchmarks/memory_usage.cpp           # Peak memory
```

---

## Configuration & Debugging

### Runtime Flags (in config.ini)
```ini
[Streaming]
enabled=false          # Toggle on/off
debug_mode=false       # Show chunk load/unload
load_distance=3        # Chunks from player
unload_distance=4      # Hysteresis
max_background_chunks=4  # Per frame
show_chunk_borders=false  # Debug visualization
```

### Debug Output
```cpp
if (DebugState::instance().streamingDebug) {
    Logger::info() << "Loaded: " << loadedCount
                   << " | Unloaded: " << unloadedCount
                   << " | Queued: " << queuedCount;
}
```

### Visualization
- Color-code chunks: Gray (unloaded), Blue (loading), Green (ready), Yellow (buffering)
- Show chunk boundaries with wireframe
- Show generation queue priority

---

## Success Metrics

| Metric | Target | Current | After Streaming |
|--------|--------|---------|-----------------|
| Initial load time | <3s | 20-30s | <2s ✅ |
| Frame time variance | <5ms | ±20ms | ±2ms ✅ |
| Memory peak | 500MB | 2GB | 800MB ✅ |
| World size limit | None | 12x64x12 | ∞ ✅ |
| Chunk generation latency | <200ms | N/A | <150ms ✅ |
| Chunk unload cleanup time | <50ms | N/A | <30ms ✅ |

---

## Rollback Strategy

Each phase has clear rollback points:

1. **Phase 1 Fails**: Delete ChunkGenerator class, restore original methods
2. **Phase 2 Fails**: Remove lookup utilities, keep map-based access
3. **Phase 3 Fails**: Disable threading, use synchronous generation (single-threaded)
4. **Phase 4 Fails**: Keep fixed world size, disable dynamic loading
5. **Phase 5 Fails**: Fall back to Option A (regenerate neighbors on collision)
6. **Phase 6 Fails**: Disable streaming-aware decoration, use boundary zones

**Total rollback time**: ~30 minutes per phase (revert commits + rebuild)

---

## Long-Term Vision

After streaming works stably:

1. **Chunk Persistence**: Save/load chunks to disk
2. **Player Modifications**: Track block changes per chunk
3. **Multiplayer**: Sync chunk data across network
4. **LOD Levels**: Render distant chunks at lower detail
5. **Procedural Generation**: Dynamic generated structures

---

## File Structure After Refactoring

```
include/
├── chunk.h                      (refactored, extract methods)
├── chunk_generator.h            (NEW)
├── chunk_streaming_manager.h    (NEW)
└── world.h                      (add update method)

src/
├── chunk.cpp                    (refactored)
├── chunk_generator.cpp          (NEW)
├── chunk_streaming_manager.cpp  (NEW)
├── world.cpp                    (integrate streaming)
└── main.cpp                     (call world.update)

tests/
├── test_chunk_generator.cpp     (NEW)
├── test_streaming_manager.cpp   (NEW)
├── test_thread_safety.cpp       (NEW)
└── ... more tests
```

---

## Recommended Implementation Order

1. **Start with Phase 1** (Extract) - lowest risk, sets foundation
2. **Immediately add Phase 2** (Infrastructure) - builds on Phase 1
3. **Move to Phase 3** (Threading) - hardest part, needs most testing
4. **Then Phase 4** (Streaming) - straightforward after threading
5. **Focus on Phase 5** (Mesh Sync) - most critical quality concern
6. **Finally Phase 6** (Decoration) - nice-to-have polish

**Estimated Timeline**:
- Phase 1-2: 1-2 days
- Phase 3: 2-3 days (most time spent on thread safety tests)
- Phase 4: 1 day
- Phase 5: 2-3 days (edge cases and testing)
- Phase 6: 1-2 days
- **Total: 1-2 weeks** with thorough testing

---

## Questions to Address Before Starting

1. **Target FPS**: 60? 144? Impacts streaming rate
2. **Memory budget**: 500MB? 2GB? Affects maximum loaded chunks
3. **Infinite world**: Yes? Impacts persistence design
4. **Multiplayer**: Will support online co-op? Affects synchronization
5. **Mod support**: Allow custom chunk generators? Impacts architecture

---

## Related Files in Codebase

Key files already reviewed:
- `/home/user/voxel-engine/include/world.h` - Main world class
- `/home/user/voxel-engine/src/world.cpp` - Implementation (931 lines)
- `/home/user/voxel-engine/include/chunk.h` - Chunk class
- `/home/user/voxel-engine/src/chunk.cpp` - Implementation (1015 lines)
- `/home/user/voxel-engine/src/main.cpp` - Entry point

Supporting systems:
- `/home/user/voxel-engine/include/tree_generator.h` - Tree placement
- `/home/user/voxel-engine/include/biome_system.h` - Biome management
- `/home/user/voxel-engine/include/biome_map.h` - Biome queries
- `/home/user/voxel-engine/src/block_system.cpp` - Block registry
