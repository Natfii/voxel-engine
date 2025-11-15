# World Bounds Expansion - Test Cases

## Overview
This document outlines the edge cases and boundary conditions tested for the expanded world bounds system.

## Coordinate System Limits

### Integer Range
- **Data Type**: `int32` for chunk coordinates
- **Range**: -2,147,483,648 to 2,147,483,647
- **Safe Maximum**: ±1,073,741,823 (INT_MAX / 2) to allow for coordinate arithmetic
- **Block Range**: ±34,359,738,368 blocks (chunk range × 32 blocks/chunk)

## Test Cases

### 1. Maximum Safe World Size
```cpp
// Test: Create world at maximum safe size
World testWorld(2147483646, 2147483646, 2147483646, 12345);
// Expected: Should succeed with validation passing
// Chunk range: ±1,073,741,823 per axis
```

### 2. Overflow Prevention
```cpp
// Test: Attempt to create world that would overflow
try {
    World testWorld(2147483647, 2147483647, 2147483647, 12345);
    // Expected: Should throw runtime_error
} catch (std::runtime_error& e) {
    // "World dimensions too large - would cause coordinate overflow"
}
```

### 3. Coordinate Validation in addStreamedChunk
```cpp
// Test: Add chunk at maximum valid coordinate
auto chunk = std::make_unique<Chunk>(1073741823, 1073741823, 1073741823);
bool result = world.addStreamedChunk(std::move(chunk));
// Expected: true (valid coordinate)

// Test: Add chunk beyond safe limit
auto invalidChunk = std::make_unique<Chunk>(1073741824, 0, 0);
bool result2 = world.addStreamedChunk(std::move(invalidChunk));
// Expected: false (overflow prevention)
```

### 4. Large But Practical World Size
```cpp
// Test: Create a very large but practical world (1 million × 1 million chunks)
World largeWorld(1000000, 64, 1000000, 12345);
// Expected: Success
// Total theoretical chunks: 64 billion
// Memory if all loaded: ~26.9 TB (impractical, requires streaming)
```

### 5. Boundary Chunk Access
```cpp
// Test: Access chunks at world boundaries
World world(4096, 64, 4096, 12345);
int halfWidth = 4096 / 2;

// Near edge chunk
Chunk* nearEdge = world.getChunkAt(halfWidth - 1, 0, 0);
// Expected: Valid chunk within bounds

// Exactly at edge (invalid)
Chunk* atEdge = world.getChunkAt(halfWidth, 0, 0);
// Expected: nullptr (out of bounds)
```

### 6. Negative Coordinate Handling
```cpp
// Test: Centered world handles negative coordinates correctly
World world(4096, 64, 4096, 12345);
int halfWidth = 4096 / 2;

// Valid negative coordinate
Chunk* negativeChunk = world.getChunkAt(-halfWidth, 0, 0);
// Expected: Valid chunk

// Too negative
Chunk* tooNegative = world.getChunkAt(-halfWidth - 1, 0, 0);
// Expected: nullptr (out of bounds)
```

## Memory Calculations

### Per-Chunk Memory Usage
- Average: 0.42 MB per chunk (with mesh data)
- Empty chunks: Much less (only metadata)

### Example World Sizes

| Dimensions (chunks) | Total Chunks | Total Blocks | Memory (if all loaded) |
|---------------------|--------------|--------------|------------------------|
| 128 × 32 × 128      | 524,288      | 17.2 million | 220 GB                 |
| 4096 × 64 × 4096    | 1.074 billion| 35.2 billion | 451 TB                 |
| 10000 × 100 × 10000 | 10 billion   | 327 billion  | 4.2 PB                 |

**Note**: Streaming system loads only nearby chunks, typically 100-1000 chunks at a time, making large worlds practical with ~0.5 GB RAM usage.

## Edge Cases Handled

### ✅ Integer Overflow Prevention
- World constructor validates dimensions before creation
- `isChunkCoordValid()` checks all chunk coordinate operations
- Safe maximum enforced: ±1,073,741,823 chunks per axis

### ✅ Boundary Checking
- World boundary checks prevent out-of-bounds chunk access
- Centered coordinate system: chunks range from -half to +half-1
- Both positive and negative coordinate ranges supported

### ✅ Coordinate Arithmetic Safety
- All world-to-chunk conversions use safe int32 math
- Tree placement coordinates validated before float-to-int cast
- Chunk streaming validates coordinates before insertion

### ✅ Practical Limits Documented
- README.md updated with world size limits
- config.ini includes helpful comments and examples
- World constructor logs actual world dimensions on creation

## Performance Considerations

### Streaming Benefits
- Only loads chunks within render distance (~100-500 chunks)
- Unloads distant chunks to maintain memory budget
- Enables "infinite" worlds without loading entire world

### Recommended Configurations
- **Small worlds** (< 1000 chunks total): Can load fully, fast startup
- **Medium worlds** (1K - 1M chunks): Use streaming, good performance
- **Large worlds** (> 1M chunks): Streaming essential, infinite exploration
- **Maximum worlds** (±1B chunks): Theoretical limit, streaming required

## Conclusion
The world bounds system now supports:
- ✅ Up to ±1 billion chunks per axis (±34 billion blocks)
- ✅ Automatic overflow prevention
- ✅ Safe coordinate validation
- ✅ Practical streaming-based approach for massive worlds
- ✅ Comprehensive documentation and examples
