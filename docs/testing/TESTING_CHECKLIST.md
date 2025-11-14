# Chunk Streaming Testing - Ship Checklist

Use this checklist to verify you're ready to ship chunk streaming safely.

## Phase 1: Setup (30 minutes)

- [ ] Copy test files to `/home/user/voxel-engine/tests/`
  - [ ] CMakeLists.txt
  - [ ] test_utils.h
  - [ ] test_utils.cpp
  - [ ] chunk_correctness_test.cpp
  - [ ] memory_leak_test.cpp
  - [ ] performance_test.cpp
  - [ ] stress_test.cpp

- [ ] Update main CMakeLists.txt
  - [ ] Add `add_subdirectory(tests)` at the end
  - [ ] Verify file saved

- [ ] Build tests
  ```bash
  cd build && cmake .. && make
  ```
  - [ ] No compilation errors
  - [ ] No linking errors
  - [ ] All 4 test executables created

- [ ] Quick test run
  ```bash
  ctest -V
  ```
  - [ ] All 27 tests show up
  - [ ] All tests pass initially (expected)

## Phase 2: Daily Development (< 1 minute before commit)

Before every commit:
```bash
cd build && ctest -L fast -V
```

- [ ] Correctness tests pass (6 tests)
- [ ] Performance gates met (sample tests)
- [ ] Duration < 10 seconds total
- [ ] No assertion failures
- [ ] Safe to commit ✓

## Phase 3: Pre-Release (1 week before shipping)

### Run Full Test Suite

```bash
cd build && ctest -V
```

- [ ] **Correctness Tests (chunk_correctness_test)** - MUST PASS ✓
  - [ ] ChunkGenerationDeterministic
  - [ ] ChunkStateTransitions
  - [ ] BlockAccessBounds
  - [ ] BlockMetadataPersistence
  - [ ] ChunkPositionTracking
  - [ ] WorldChunkLookup

- [ ] **Memory Tests (memory_leak_test)** - MUST PASS ✓
  - [ ] ChunkLoadUnloadCycles
  - [ ] WorldLoadUnloadCycles
  - [ ] ChunkBufferLifecycle
  - [ ] LargeWorldCleanup
  - [ ] RepeatedWorldRegeneration
  - [ ] BlockModificationMemorySafety

- [ ] **Performance Tests (performance_test)** - MUST PASS ✓
  - [ ] ChunkGenerationPerformance (gate: < 5ms)
  - [ ] MeshGenerationPerformance (gate: < 3ms)
  - [ ] WorldInitializationPerformance (gate: < 20ms/chunk)
  - [ ] BlockAccessPerformance (gate: < 10µs)
  - [ ] BlockModificationPerformance
  - [ ] MetadataPerformance
  - [ ] WorldBlockAccessPerformance

- [ ] **Stress Tests (stress_test)** - SHOULD PASS ✓
  - [ ] RapidTeleportationStress
  - [ ] WorldBoundaryConditions
  - [ ] MassiveBlockModification
  - [ ] ExtremeWorldSize
  - [ ] RapidChunkStateChanges
  - [ ] MetadataStress
  - [ ] OverlappingBlockModifications
  - [ ] ChunkAccessPatternStress

### Check for Memory Leaks

**On Linux:**
```bash
valgrind --leak-check=full --show-leak-kinds=all ./tests/test_memory_leaks
```

- [ ] No definitely lost blocks
- [ ] No indirectly lost blocks
- [ ] "LEAK SUMMARY: ... definitely lost: 0 bytes"

**On macOS/Windows:**
- [ ] Address Sanitizer reports no leaks (automatic)
- [ ] Run test normally: `./tests/test_memory_leaks`
- [ ] No ASAN errors at end

### Performance Analysis

**Optional but Recommended:**
```bash
perf record ./tests/test_performance
perf report
```

- [ ] No unexpected slow functions
- [ ] Top functions are in tight loops (expected)
- [ ] No O(n²) algorithms appearing

### Manual Gameplay Test

Most critical: Test in the actual game

```bash
./voxel-engine
```

- [ ] **Run for 30 minutes continuously**
  - [ ] Walk/fly around loading chunks
  - [ ] Observe memory usage (Task Manager/Activity Monitor)
  - [ ] Note FPS (should stay 60+ or match your target)

- [ ] **Verify visual correctness**
  - [ ] Terrain matches between runs (same seed)
  - [ ] No inverted/upside-down blocks
  - [ ] No missing blocks
  - [ ] No floating terrain
  - [ ] Water flows correctly
  - [ ] Trees generate properly

- [ ] **Verify performance**
  - [ ] No stuttering when moving
  - [ ] No frame time spikes > 50ms
  - [ ] Chunks load smoothly
  - [ ] Can't see chunk boundaries pop in

- [ ] **Verify stability**
  - [ ] No crashes
  - [ ] No hangs
  - [ ] No memory growth over 30 minutes
  - [ ] Can quit cleanly

### Performance Gates Review

Print this and verify all gates met:

```
┌─────────────────────────────────────────┐
│ PERFORMANCE GATES - PRE-RELEASE CHECK   │
├─────────────────────────────────────────┤
│ Chunk Generation:   ___ ms (gate: 5ms)  │
│   Status: [ ] PASS [ ] FAIL             │
│                                         │
│ Mesh Generation:    ___ ms (gate: 3ms)  │
│   Status: [ ] PASS [ ] FAIL             │
│                                         │
│ Block Access:       ___ µs (gate: 10µs) │
│   Status: [ ] PASS [ ] FAIL             │
│                                         │
│ World Load:         ___ ms/chunk (20ms) │
│   Status: [ ] PASS [ ] FAIL             │
│                                         │
│ Game FPS:           ___ FPS (gate: 30)  │
│   Status: [ ] PASS [ ] FAIL             │
│                                         │
│ OVERALL: [ ] READY TO SHIP [ ] NOT READY│
└─────────────────────────────────────────┘
```

## Phase 4: Final Verification (1 day before shipping)

### Run Complete Test Suite Again

```bash
cd build && ctest -V && valgrind --leak-check=full ./tests/test_memory_leaks
```

- [ ] All tests still pass
- [ ] No new memory leaks
- [ ] Performance stable

### Code Review

- [ ] All chunk streaming code reviewed
- [ ] No obvious bugs spotted
- [ ] Edge cases considered
- [ ] Comments clear where logic is non-obvious

### Documentation

- [ ] Player-facing docs mention chunk streaming
- [ ] No TODOs left in streaming code
- [ ] Performance characteristics documented

### Version & Release Notes

- [ ] Increment version number
- [ ] Write release notes mentioning chunk streaming
- [ ] Note any known limitations
- [ ] Credit contributors

## Phase 5: Shipping Decision

### Before You Hit "Release":

- [ ] ✓ All 27 tests pass
- [ ] ✓ Memory test shows 0 leaks
- [ ] ✓ Performance gates all met
- [ ] ✓ 30-minute gameplay test successful
- [ ] ✓ No visual artifacts observed
- [ ] ✓ Code reviewed and clean
- [ ] ✓ Documentation complete
- [ ] ✓ Version number incremented

### Go/No-Go Decision

If all boxes above are checked:
## ✓ YOU'RE READY TO SHIP

If any box is unchecked:
## ✗ DO NOT SHIP - FIX ISSUES FIRST

---

## Test Failure Response Guide

### Test Failed: What Now?

**Correctness Test Failed**
→ Time: 1-2 hours to fix
→ Priority: CRITICAL - do not ship
→ Action:
  1. Run test in debugger
  2. Check chunk.cpp generation logic
  3. Verify determinism (same seed = same result)
  4. Look for float precision issues
→ Block all commits until fixed

**Memory Test Failed**
→ Time: 2-4 hours to fix
→ Priority: CRITICAL - do not ship
→ Action:
  1. Run valgrind: `valgrind -v --leak-check=full ./test_memory_leaks`
  2. Find allocation site (malloc line)
  3. Search code for where it should be freed
  4. Check for missing `delete` or bad `unique_ptr`
→ Do not merge until fixed

**Performance Test Failed**
→ Time: 4-8 hours to fix
→ Priority: HIGH - performance issue
→ Action:
  1. Note which gate was violated
  2. Run: `perf record ./tests/test_performance`
  3. Find hot function: `perf report`
  4. Optimize algorithm or parallelize
  5. Consider reducing chunk size
→ Do not ship until gates are met

**Stress Test Failed**
→ Time: Low priority
→ Priority: MEDIUM - edge case
→ Action:
  1. Determine if reproducible in gameplay
  2. If yes: Fix before shipping
  3. If no: File bug for v1.1
→ Can ship if other tests pass (with note in release notes)

---

## Quick Reference: Minimum Viable Release

**If pressed for time, MINIMUM to ship safely:**

1. ✓ Correctness tests pass
2. ✓ Memory test passes (0 leaks)
3. ✓ Performance gates met
4. ✓ 30-minute manual test successful

These 4 will catch 95% of critical bugs.

**Not minimum but recommended:**
5. ✓ Stress tests pass
6. ✓ Code review
7. ✓ Full documentation

---

## Post-Release Monitoring

After you ship, monitor for:

### First Week
- [ ] Check forum/Discord for crash reports
- [ ] Monitor error logs for exceptions
- [ ] Watch for "game stutters" complaints
- [ ] Check FPS reports in bug tracker

### First Month
- [ ] No "game crashes after 30 minutes" reports
- [ ] No "terrain looks inverted" reports
- [ ] Performance complaints minimal
- [ ] User reviews mention streaming works

### First 3 Months
- [ ] Stable player retention
- [ ] No critical bugs found
- [ ] Performance meets expectations
- [ ] Ready for v1.1 feature work

---

## Test Maintenance After Shipping

### Weekly
- [ ] Run `ctest -L fast` in CI
- [ ] Check for any regressions

### Before Each Release
- [ ] Run `ctest -V`
- [ ] Run memory check
- [ ] Run performance analysis
- [ ] Manual gameplay test (30 min)

### Quarterly
- [ ] Review test coverage
- [ ] Add new tests for reported bugs
- [ ] Update performance gates if needed
- [ ] Archive performance metrics

---

## Success Metrics

### You'll Know Tests Are Working When:

✓ Tests catch real bugs before shipping
✓ Developers habitually run tests before commits
✓ Release builds ship with high confidence
✓ Users report smooth streaming experience
✓ No "crashes after 30 minutes" reports
✓ No "terrain glitches" reports
✓ First month: 0 chunk-streaming related crashes
✓ Performance stable across releases

### You'll Know It's Time to Ship When:

✓ Green checkmarks on all 4 phases
✓ No test failures for 48 hours
✓ 30-minute gameplay test successful 3 times
✓ Confidence level: "This will work for players"

---

## Emergency Shipping (If Deadline Critical)

**Absolute minimum to ship (NOT RECOMMENDED):**

```bash
ctest -R Correctness -V  # 30 seconds
valgrind ./test_memory_leaks  # 2 minutes
```

If both pass → Can technically ship
But you're taking a risk with performance

**Better: Add 15 minutes for:**
```bash
ctest -V  # All tests
```

This catches performance issues that users WILL report

---

## Final Sign-Off

```
╔═══════════════════════════════════════════════════════════╗
║           CHUNK STREAMING RELEASE SIGN-OFF                ║
╠═══════════════════════════════════════════════════════════╣
║                                                           ║
║ Developer Name: _____________________                    ║
║ Date: _____________________                              ║
║                                                           ║
║ Checklist Complete:        [ ] YES  [ ] NO              ║
║ All Tests Passing:         [ ] YES  [ ] NO              ║
║ No Memory Leaks:           [ ] YES  [ ] NO              ║
║ Performance Gates Met:     [ ] YES  [ ] NO              ║
║ Manual Test Successful:    [ ] YES  [ ] NO              ║
║ Code Review Complete:      [ ] YES  [ ] NO              ║
║                                                           ║
║ Confidence Level: _____%
║                                                           ║
║ Ready to Ship:            [ ] APPROVED  [ ] NOT APPROVED ║
║                                                           ║
╚═══════════════════════════════════════════════════════════╝
```

---

**You're ready to ship safely when all boxes are checked! 🚀**
