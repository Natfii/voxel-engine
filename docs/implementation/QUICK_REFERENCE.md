# Chunk Streaming Refactoring: Quick Reference

**Quick lookup guide for implementation decisions and code patterns.**

---

## Phase Checklists

### Phase 1: Extract ChunkGenerator
- [ ] Create `include/chunk_generator.h`
- [ ] Create `src/chunk_generator.cpp`
- [ ] Copy `Chunk::generate()` → `ChunkGenerator::generateTerrain()`
- [ ] Copy `Chunk::generateMesh()` → `ChunkGenerator::generateMesh()`
- [ ] Copy tree placement → `ChunkGenerator::decorateChunk()`
- [ ] Update `World::generateWorld()` to use ChunkGenerator
- [ ] Run tests: All pass ✅
- [ ] **Commit**: "Extract chunk generation into ChunkGenerator"

---

## Lock Pattern: Reader-Writer Mutex

```cpp
// Render thread (READER):
{
    std::shared_lock lock(m_chunkMapMutex);  // Multiple readers OK
    for (auto& chunk : m_chunkMap) {
        if (isVisible(chunk)) visibleChunks.push_back(chunk.get());
    }
}  // Lock released, generation thread can write

// Generation thread (WRITER):
{
    std::unique_lock lock(m_chunkMapMutex);  // Exclusive access
    m_chunkMap[coord] = chunk;  // Insert (~1ms)
}  // Lock released
```

---

## Key Principles

1. **Heavy work WITHOUT lock** (Generation: 50-100ms)
2. **Brief lock for insert/remove** (<1ms)
3. **Single-threaded GPU buffering** (Main thread only)
4. **Chunk state machine** (NONE → TERRAIN → MESHES → BUFFERED)

---

## Performance Targets

| Metric | Before | After | Test Method |
|--------|--------|-------|-------------|
| **Startup** | 20-30s | <2s | Time first frame |
| **Memory** | 2GB | <800MB peak | `valgrind --tool=massif` |
| **FPS** | N/A | 60±1 | Game loop timer |
| **Lock time** | N/A | <1ms/frame | ThreadSanitizer |

---

## Common Pitfalls

❌ **Holding lock during heavy work** → Release, do work, re-acquire
❌ **Forgetting RAII** → Always use `{ std::unique_lock lock(...); }`
❌ **Accessing chunk after unload** → Check `getChunkAt()` returns non-null
❌ **Mesh without neighbors** → Check `checkAllNeighborsHaveTerrain()`
❌ **Tree spans boundary** → Use `BOUNDARY_ZONE` constant

---

## Test Commands

```bash
# Build with thread safety
cmake -DCMAKE_CXX_FLAGS="-fsanitize=thread" ..
cmake --build .

# Run tests
ctest --output-on-failure

# Profile performance
perf record -g ./voxel-engine
perf report

# Check for memory leaks
valgrind --leak-check=full ./voxel-engine
```

---

## Emergency Fixes

**Race condition detected?** → See CONCURRENCY_ANALYSIS.md
**Mesh generation crashed?** → Check neighbor chunks exist
**Memory leak on unload?** → Call `destroyBuffers()` before erase
**FPS drops?** → Use `perf` to find bottleneck

---

See main documents:
- `REFACTORING_STRATEGY.md` - Full strategy
- `CONCURRENCY_ANALYSIS.md` - Threading details
- `STREAMING_REFACTORING_SUMMARY.md` - Executive summary
