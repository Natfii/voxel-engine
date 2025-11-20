# Chunk Streaming Testing - Quick Reference Card

## Before Every Commit (30 seconds)

```bash
cd build
ctest -L fast -V
```

**Expected:** All tests pass in < 10 seconds
**If fails:** Do not commit - investigate error

---

## Before Release Build (5 minutes)

```bash
# Run all tests
ctest -V

# Check memory on Linux
valgrind --leak-check=full ./tests/test_memory_leaks
```

**Expected:**
- All 4 tests pass
- No memory leaks reported
- Performance gates not exceeded

---

## Useful Test Commands

| Task | Command |
|------|---------|
| Run all tests | `ctest -V` |
| Run only fast tests | `ctest -L fast -V` |
| Run only memory tests | `ctest -L memory -V` |
| Run one test | `ctest -R ChunkCorrectness -V` |
| Stop on first failure | `ctest --stop-on-failure -V` |
| Show only failures | `ctest --output-on-failure` |
| Check memory on Linux | `valgrind ./tests/test_memory_leaks` |
| Profile performance | `perf record ./tests/test_performance && perf report` |

---

## Performance Gates (MUST NOT VIOLATE)

| Metric | Limit | If Violated |
|--------|-------|------------|
| Chunk generation | < 5ms | Ship will stutter loading chunks |
| Mesh generation | < 3ms | Ship will stutter loading chunks |
| Block access | < 10 µs | World interaction will be laggy |
| World load | < 20ms/chunk | Initial load takes too long |
| Frame time | < 33ms | 30 FPS minimum is too low |

---

## Test Results Interpretation

### ✓ All Tests Pass
- Safe to commit
- Safe to release
- Performance acceptable

### ✗ Correctness Test Fails
**CRITICAL** - Do not ship
- Same seed != same terrain
- Chunk state machine broken
- Silent world generation bug

### ✗ Memory Test Fails
**CRITICAL** - Do not ship
- Memory leak will cause crashes after 10-30 min
- Use Valgrind to find leaking code
- Check for missing `delete`, improper `unique_ptr`

### ✗ Performance Test Fails
**WARNING** - Fix before shipping
- Players will experience stuttering
- Profile code to find bottleneck
- May need algorithmic changes

### ✗ Stress Test Fails
**NOTE** - Can release with this noted
- Edge case not handled
- File bug for next version
- Low priority unless reproducible in gameplay

---

## Adding a New Test

1. Create test function:
```cpp
TEST(MyTest) {
    ASSERT_TRUE(condition);
    std::cout << "✓ Description\n";
}
```

2. Choose file:
   - Correctness → `chunk_correctness_test.cpp`
   - Memory → `memory_leak_test.cpp`
   - Performance → `performance_test.cpp`
   - Stress → `stress_test.cpp`

3. Rebuild and run:
```bash
cd build && make && ctest -R MyTest -V
```

---

## Debugging a Test Failure

**Step 1: Read the error message**
```
ASSERT_EQ failed: c.getBlock(5, 10, 15) != 1
```
→ Block access returning wrong value

**Step 2: Isolate the test**
```bash
ctest -R ChunkCorrectness -V
```

**Step 3: Add debug output**
Edit test file, add `std::cout` around assertion

**Step 4: Run with debugger**
```bash
gdb ./tests/test_chunk_correctness
(gdb) run
(gdb) bt  # backtrace to see call stack
```

**Step 5: Check implementation**
→ Look at `chunk.cpp` where the bug likely is

---

## Memory Leak Debugging

### On Linux with Valgrind:

```bash
valgrind --leak-check=full --show-leak-kinds=all ./tests/test_memory_leaks 2>&1 | less
```

Look for:
```
LEAK SUMMARY:
    definitely lost: 0 bytes
    indirectly lost: 0 bytes
```
Both should be 0

If not:
```
40 bytes in 1 blocks are definitely lost in loss record 1
    at 0x...: malloc (...)
    by 0x...: World::World() (world.cpp:...)
```
→ Check world.cpp line XX for unfreed memory

### With Address Sanitizer:

Just run test normally - ASAN reports leaks at exit:
```bash
./tests/test_memory_leaks
```

Output shows:
```
==1234==ERROR: LeakSanitizer: detected memory leaks
    Leak at: chunk.cpp:123
```

---

## Performance Profiling

### Linux with `perf`:

```bash
perf record ./tests/test_performance
perf report
# Shows % time in each function
```

### macOS with Instruments:

```bash
instruments -t "Time Profiler" ./tests/test_performance
```

### All platforms with `gprof`:

```bash
./tests/test_performance  # Generates gmon.out
gprof ./tests/test_performance gmon.out | head -50
```

---

## Integration Checklist

Before you start shipping chunk streaming:

- [ ] Tests directory created: `/home/user/voxel-engine/tests/`
- [ ] `CMakeLists.txt` in tests directory
- [ ] `test_utils.h` and `test_utils.cpp`
- [ ] `chunk_correctness_test.cpp`
- [ ] `memory_leak_test.cpp`
- [ ] `performance_test.cpp`
- [ ] `stress_test.cpp`
- [ ] Main `CMakeLists.txt` updated with `add_subdirectory(tests)`
- [ ] All tests build without errors
- [ ] All tests pass: `ctest -V`
- [ ] Memory check passes: `valgrind ./tests/test_memory_leaks`

---

## Common Build Errors

### "Vulkan headers not found"
→ Check: `find_package(Vulkan REQUIRED)` in main CMakeLists.txt

### "Unknown type name 'Chunk'"
→ Check: `#include "chunk.h"` in test file

### "Multiple definition of main"
→ Check: Don't include `src/main.cpp` in test executables

### "Undefined reference to MockVulkanRenderer"
→ Check: `test_utils.cpp` is compiled into executable

---

## File Locations

```
/home/user/voxel-engine/
├── docs/
│   └── testing/
│       ├── TESTING_STRATEGY.md           ← Design docs
│       ├── TESTING_INTEGRATION.md        ← Setup guide
│       ├── TESTING_QUICK_REFERENCE.md    ← THIS FILE
│       ├── TESTING_CHECKLIST.md          ← Ship checklist
│       ├── TESTING_IMPLEMENTATION_SUMMARY.md
│       ├── TESTING_ARCHITECTURE.txt
│       └── README_TESTING.md
├── tests/
│   ├── CMakeLists.txt
│   ├── test_utils.h
│   ├── test_utils.cpp
│   ├── chunk_correctness_test.cpp
│   ├── memory_leak_test.cpp
│   ├── performance_test.cpp
│   └── stress_test.cpp
└── CMakeLists.txt                        ← Update with add_subdirectory(tests)
```

---

## One-Liner Commands

```bash
# Build and run all tests
cmake --build build && cd build && ctest -V

# Clean rebuild and test
rm -rf build && mkdir build && cd build && cmake .. && make && ctest -V

# Quick check before commit
cd build && ctest -L fast -V

# Full pre-release check
cd build && ctest -V && valgrind --leak-check=full ./tests/test_memory_leaks

# Find performance bottleneck (Linux)
cd build && perf record ./tests/test_performance && perf report | head -20

# Test a specific feature
cd build && ctest -R "ChunkGeneration" -V
```

---

## When in Doubt

1. Read the assertion error message - it tells you what's wrong
2. Run the failing test in isolation: `ctest -R TestName -V`
3. Add `std::cout` debug output around the assertion
4. Check the corresponding implementation file (e.g., chunk.cpp for chunk tests)
5. If memory issue: run with `valgrind --leak-check=full`
6. If performance issue: run with `perf record` then `perf report`

---

## Success Criteria for Shipping

✓ All correctness tests pass
✓ All memory tests pass (0 leaks)
✓ All performance gates met
✓ Player can stream for 30 min without stuttering
✓ No visual artifacts (inverted terrain, missing blocks)
✓ No crashes from edge cases

**Go live!** 🚀

---

Last updated: 2025-11-20
For detailed information, see `TESTING_STRATEGY.md`
