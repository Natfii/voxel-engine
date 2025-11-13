# Testing Integration Guide

This guide explains how to integrate the testing infrastructure into your voxel-engine project.

## Quick Start (5 minutes)

### Step 1: Update Main CMakeLists.txt

Add this to the end of `/home/user/voxel-engine/CMakeLists.txt`:

```cmake
# Enable testing infrastructure
enable_testing()
add_subdirectory(tests)
```

### Step 2: Build and Run Tests

```bash
# Build the project with tests
cd /home/user/voxel-engine/build
cmake ..
make

# Run all tests
ctest --verbose

# Run only fast tests
ctest -L fast -V

# Run specific test
ctest -R ChunkCorrectness -V
```

### Step 3: Check Memory Leaks (Optional but Recommended)

**On Linux with Valgrind:**
```bash
valgrind --leak-check=full --show-leak-kinds=all ./tests/test_memory_leaks
```

**With Address Sanitizer (automatic with our CMake):**
```bash
./tests/test_memory_leaks
# Output will show any memory issues
```

## Detailed Setup Instructions

### Integrating tests/CMakeLists.txt

The `tests/CMakeLists.txt` file defines all test executables and their dependencies. It assumes:

1. **Source files are in `src/`** - All `.cpp` files except `main.cpp`
2. **Headers are in `include/`** - All `.h` files
3. **External dependencies** - ImGui, Vulkan, GLFW, yaml-cpp (same as main build)

### Resolving Build Issues

**Issue: "Vulkan headers not found"**
→ Check that `Vulkan_INCLUDE_DIRS` is set in parent CMakeLists.txt ✓

**Issue: "Undefined reference to VulkanRenderer"**
→ We use mock Vulkan objects in tests
→ Add `tests/test_utils.cpp` to CMakeLists.txt ✓

**Issue: "Multiple definition of main()"**
→ Each test file has its own `main()`
→ Don't include `src/main.cpp` in test builds ✓

### Test File Organization

```
tests/
├── CMakeLists.txt                 # Test build configuration
├── test_utils.h                   # Shared testing utilities
├── test_utils.cpp                 # Mock object implementations
├── chunk_correctness_test.cpp     # Chunk generation correctness tests
├── memory_leak_test.cpp           # Memory leak detection tests
├── performance_test.cpp           # Performance gate tests
└── stress_test.cpp                # Stress and edge case tests
```

## Running Tests

### Command Reference

```bash
# Run all tests with verbose output
ctest --verbose

# Run all tests, stop on first failure
ctest --stop-on-failure

# Run only tests with specific label
ctest -L fast -V        # Fast tests only
ctest -L memory -V      # Memory tests only
ctest -L performance -V # Performance tests only

# Run specific test
ctest -R ChunkCorrectness -V

# Run tests with timeout
ctest --timeout 60 -V

# Show test properties
ctest --show-details PASSED
ctest --show-details FAILED
```

### Expected Output

All tests should pass:

```
========================================
ChunkCorrectness
========================================
Test project /home/user/voxel-engine/build
    Start  1: ChunkCorrectness
1/1 Test #1: ChunkCorrectness ............   Passed    0.15 sec

    Start  2: MemoryLeaks
2/4 Test #2: MemoryLeaks ................   Passed    2.34 sec

    Start  3: Performance
3/4 Test #3: Performance ................   Passed    1.23 sec

    Start  4: Stress
4/4 Test #4: Stress .....................   Passed    1.45 sec

100% tests passed, 0 tests failed out of 4
```

## Interpreting Test Failures

### Correctness Test Failures

If `ChunkCorrectness` fails:
- ✗ Same seed should produce identical terrain
- ✗ Chunk state transitions broken
- ✗ Block access/bounds checking issue

**Action:** These are critical bugs - do not ship
→ Use debugger to trace generation logic
→ Check for float precision issues, off-by-one errors

### Memory Test Failures

If `MemoryLeaks` fails:
- ✗ Memory not being freed properly
- ✗ Double-free or use-after-free
- ✗ Memory corruption

**Action:** Do not ship until fixed
→ Run with full Valgrind trace: `valgrind -v --leak-check=full ./test_memory_leaks 2>&1 | less`
→ Look for `LEAK SUMMARY` section
→ Check for missing `delete`, improper `unique_ptr` usage

### Performance Test Failures

If `Performance` fails:
- ✗ Chunk generation > 5ms
- ✗ Mesh generation > 3ms
- ✗ World loading > 20ms per chunk

**Action:** Performance issue - shipping may cause stuttering
→ Profile with `perf record ./test_performance && perf report`
→ Check for algorithmic inefficiencies
→ May need to reduce chunk size or optimize tight loops

### Stress Test Failures

If `Stress` fails:
- ✗ Edge cases not handled
- ✗ Potential race conditions (if threading)
- ✗ Boundary conditions broken

**Action:** May be acceptable for initial release if main tests pass
→ Investigate and file bug for later
→ Check if failure is reproducible

## Integration with CI/CD

### GitHub Actions Example

Create `.github/workflows/tests.yml`:

```yaml
name: Tests

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2

      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y libvulkan-dev libglfw3-dev

      - name: Configure
        run: |
          mkdir build
          cd build
          cmake ..

      - name: Build
        run: cd build && make

      - name: Run Tests
        run: cd build && ctest --verbose

      - name: Memory Check
        run: |
          sudo apt-get install -y valgrind
          cd build
          valgrind --leak-check=full ./tests/test_memory_leaks
```

### Pre-commit Hook

Create `.git/hooks/pre-commit`:

```bash
#!/bin/bash
# Run fast tests before commit

cd "$(git rev-parse --show-toplevel)"

# Build if not already built
if [ ! -d build ]; then
    mkdir build
    cd build
    cmake ..
    make
    cd ..
fi

# Run only fast tests
cd build
ctest -L fast -V

if [ $? -ne 0 ]; then
    echo "Tests failed! Commit aborted."
    exit 1
fi

echo "All tests passed."
exit 0
```

Make it executable:
```bash
chmod +x .git/hooks/pre-commit
```

## Performance Benchmarking

### Running Performance Tests Only

```bash
ctest -L performance -V
```

### Expected Performance Gates

```
Chunk generation:  < 5ms per chunk
Mesh generation:   < 3ms per chunk
World loading:     < 20ms per chunk average
Block access:      < 10 µs per access
```

### If Gates Are Violated

1. **Profile the code:**
   ```bash
   perf record ./tests/test_performance
   perf report
   ```

2. **Check for bottlenecks:**
   - Look at time spent in hot loops
   - Check for cache misses
   - Verify no excessive allocations

3. **Optimization strategies:**
   - Reduce chunk size (tradeoff: more draw calls)
   - Optimize mesh generation algorithm
   - Use SIMD if available
   - Parallelize more aggressively

## Customizing Tests

### Adding a New Test

1. Create test function in appropriate file:

```cpp
TEST(MyNewTest) {
    // Test implementation
    ASSERT_TRUE(condition);
    std::cout << "✓ Test description\n";
}
```

2. Tests auto-register via `TEST()` macro
3. Run: `ctest -R MyNewTest -V`

### Modifying Performance Gates

Edit the `ASSERT_LT` lines in `performance_test.cpp`:

```cpp
// Change from 5ms to 3ms per chunk
ASSERT_LT(stats.average_ms, 3.0);  // Was 5.0
```

### Adding Test Categories

Edit test properties in `tests/CMakeLists.txt`:

```cmake
set_tests_properties(MyTest PROPERTIES
    TIMEOUT 30
    LABELS "fast;custom"  # Add custom label
)
```

Then run with:
```bash
ctest -L custom -V
```

## Troubleshooting

### Tests Won't Build

**Error: "chunk.h: No such file or directory"**
→ Update `target_include_directories()` in tests/CMakeLists.txt
→ Verify include paths match main build

**Error: "undefined reference to..."**
→ Add missing `.cpp` files to test executable
→ Check that all dependencies are linked

### Tests Crash Immediately

**Error: "Segmentation fault"**
→ Mock objects may not be initialized
→ Check that static objects are created in test_utils.cpp

**Error: "Assertion failed"**
→ This is expected if test is checking for failure
→ Read assertion message carefully

### Memory Tests Run Forever

**Issue: Test hangs**
→ May be infinite loop in generation
→ Kill with Ctrl+C and check which test number it's on
→ Add timeout: `ctest --timeout 10 -V`

## Best Practices

1. **Run before every commit:**
   ```bash
   ctest -L fast -V  # 5-10 seconds
   ```

2. **Run full suite before release:**
   ```bash
   ctest -V          # 1-2 minutes
   ```

3. **Check memory before shipping:**
   ```bash
   valgrind --leak-check=full ./tests/test_memory_leaks
   ```

4. **Profile performance regularly:**
   ```bash
   perf record ./tests/test_performance
   perf report
   ```

5. **Keep tests fast:**
   - Fast tests < 5 seconds total
   - Memory tests < 2 minutes
   - Performance tests < 1 minute
   - Stress tests can be longer

## Next Steps

1. ✓ Copy test files to `tests/` directory
2. ✓ Update main `CMakeLists.txt` with `add_subdirectory(tests)`
3. ✓ Build: `cmake .. && make`
4. ✓ Run: `ctest --verbose`
5. ✓ Fix any compilation errors
6. ✓ Fix any test failures
7. → Ready to ship!

For questions, see `TESTING_STRATEGY.md` for detailed design rationale.
