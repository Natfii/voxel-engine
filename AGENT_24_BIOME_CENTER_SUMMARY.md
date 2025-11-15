# Agent 24: Biome Center/Voronoi System Implementation

## Summary

Implemented a Voronoi-based biome clustering system that creates natural biome distributions with clear centers and boundaries. This provides an alternative to the traditional noise-based biome selection.

## Implementation Overview

### 1. Center Generation Algorithm

**File**: `/home/user/voxel-engine/src/biome_voronoi.cpp`

The system uses a **grid-based Poisson-disc-like distribution** to generate biome centers:

```cpp
// Grid-based center placement with jitter
- Base spacing: 400 blocks (configurable)
- Jitter range: ±30% of spacing (±120 blocks)
- Prevents perfect grid alignment
- Deterministic generation (same seed = same centers)
```

**Key Features**:
- **Spatial Coherence**: Centers are evenly distributed across the world
- **Jittered Positions**: Random offsets prevent geometric patterns
- **Climate-based Selection**: Each center's biome is determined by temperature/moisture noise at that location
- **Cached Generation**: Centers are generated on-demand and cached for performance

**Algorithm**:
1. Divide world into grid cells (400x400 blocks by default)
2. For each grid cell, generate one center point
3. Apply jitter using noise (±120 blocks from grid center)
4. Sample climate noise at center position
5. Select appropriate biome based on temperature & moisture
6. Cache center for future lookups

### 2. Distance Calculation Method

**Voronoi Partitioning with Smooth Blending**:

```cpp
float BiomeVoronoi::calculateVoronoiWeight(float distance, float minDistance, float blendRadius)
```

The distance calculation uses **distorted Euclidean distance** to prevent geometric Voronoi cells:

1. **Position Distortion**: Apply noise-based warping to query position
   ```cpp
   distortionStrength = spacing * 0.15f  // 15% of center spacing
   offsetX = distortionNoise->GetNoise(x, z) * distortionStrength
   distortedPos = originalPos + offset
   ```

2. **Distance to Centers**: Calculate Euclidean distance from distorted position to each center
   ```cpp
   distance = sqrt(dx*dx + dz*dz)
   ```

3. **Weight Calculation**: Smooth exponential falloff in blend zone
   ```cpp
   blendFactor = (distance - minDistance) / blendRadius
   weight = exp(-3.0 * blendFactor^2)
   ```

**Blend Radius**: Default 80 blocks
- Points within 80 blocks of a center receive high weight
- Smooth falloff creates natural transitions
- Multiple centers can influence a single point

### 3. How This Improves Biome Distribution

**Advantages over Pure Noise-Based Selection**:

#### A. Natural Clustering
- **Coherent Regions**: Each biome has a clear "heart" or center
- **Identifiable Territories**: Players can navigate "from forest to desert" meaningfully
- **Geographic Realism**: Biomes cluster like real-world ecosystems

#### B. Predictable Yet Varied
- **Center Points**: Consistent biome locations for given seed
- **Noise Distortion**: Prevents perfectly circular or hexagonal boundaries
- **Organic Boundaries**: Natural-looking biome transitions

#### C. Better Exploration Experience
- **Biome Discovery**: Players can locate and explore distinct biome regions
- **Navigation**: Clearer sense of biome territories
- **Reduced Noise**: Less chaotic biome mixing compared to pure noise

#### D. Performance Benefits
- **Spatial Lookup**: Efficient nearest-neighbor search
- **Cached Centers**: Generate once, reuse forever
- **Configurable Density**: Adjust center spacing for desired biome size

### 4. Configuration Options

**Biome Size Control**:
```cpp
biomeVoronoi->setCenterSpacing(spacing);
// 200 blocks: Small, Minecraft-like biomes
// 400 blocks: Medium biomes (default)
// 800 blocks: Large biomes
// 1600 blocks: Massive biomes
```

**Transition Smoothness**:
```cpp
biomeVoronoi->setBlendRadius(radius);
// 40 blocks: Sharp transitions
// 80 blocks: Smooth transitions (default)
// 120 blocks: Very gradual transitions
```

**Enable/Disable**:
```cpp
biomeMap->setVoronoiMode(true);   // Enable Voronoi centers
biomeMap->setVoronoiMode(false);  // Use traditional noise
```

### 5. Noise-Based Distortion

**Prevents Geometric Patterns**:

The system uses **multi-layer noise distortion** to break up perfectly geometric Voronoi cells:

1. **Center Jitter**: ±30% random offset from grid positions
2. **Position Warping**: Query positions are distorted before distance calculation
3. **Smooth Variation**: Uses OpenSimplex2 noise for natural distortion

**Distortion Noise Configuration**:
```cpp
// X-axis distortion
m_distortionNoiseX->SetFrequency(0.002f);  // ~500 block features
m_distortionNoiseX->SetFractalOctaves(3);  // Multi-scale variation

// Z-axis distortion (independent)
m_distortionNoiseZ->SetFrequency(0.002f);
m_distortionNoiseZ->SetFractalOctaves(3);
```

**Result**: Organic, natural-looking biome boundaries that avoid the "hexagonal tile" look of pure Voronoi.

## Visual Results

### Biome Distribution Characteristics

**Without Voronoi (Traditional Noise)**:
- Smooth gradients between biomes
- No clear biome centers
- Climate-based distribution
- Can feel "blobby" or chaotic

**With Voronoi Centers**:
- Distinct biome regions with centers
- Clear territorial boundaries
- Organic shapes (not geometric)
- Better exploration landmarks

### Expected Patterns

1. **Biome Clusters**: Similar biomes group around their centers
2. **Natural Boundaries**: Smooth transitions at Voronoi edges (not sharp lines)
3. **Varied Sizes**: Jitter creates different-sized biome regions
4. **No Geometric Artifacts**: Distortion prevents hexagonal patterns

## Integration with BiomeMap

The Voronoi system is **integrated but optional**:

```cpp
// In BiomeMap constructor
m_voronoi = std::make_unique<BiomeVoronoi>(seed);
m_useVoronoiMode = false;  // Disabled by default

// When enabled, getBiomeInfluences() uses Voronoi:
if (m_useVoronoiMode && m_voronoi) {
    auto centers = m_voronoi->findNearestCenters(worldX, worldZ, 4);
    // Calculate weights based on distance to centers
    // ...
}
// Otherwise, use traditional noise-based selection
```

**Backward Compatible**:
- Default behavior unchanged (noise-based)
- Can toggle at runtime
- Shared caching system for performance

## Technical Details

### Files Created
- `/home/user/voxel-engine/include/biome_voronoi.h` - Voronoi system interface
- `/home/user/voxel-engine/src/biome_voronoi.cpp` - Voronoi implementation

### Files Modified
- `/home/user/voxel-engine/include/biome_map.h` - Added Voronoi integration
- `/home/user/voxel-engine/src/biome_map.cpp` - Integrated Voronoi mode

### Key Classes

**BiomeCenter**: Represents a biome center point
```cpp
struct BiomeCenter {
    glm::vec2 position;     // World position (X, Z)
    const Biome* biome;     // Biome at this center
    float temperature;      // Climate values
    float moisture;
    int id;                 // Unique identifier
};
```

**BiomeVoronoi**: Main Voronoi system
```cpp
class BiomeVoronoi {
    // Generate centers in a region
    std::vector<BiomeCenter> getCentersInRegion(...);

    // Find nearest centers to a point
    std::vector<std::pair<BiomeCenter, float>> findNearestCenters(...);

    // Calculate Voronoi weight
    float calculateVoronoiWeight(distance, minDist, blendRadius);

    // Get distorted position (prevents geometric patterns)
    glm::vec2 getDistortedPosition(worldX, worldZ);
};
```

## Performance Characteristics

### Time Complexity
- **Center Lookup**: O(k) where k = number of centers in search radius (~9 grid cells)
- **Nearest Neighbor**: O(k log k) for sorting by distance
- **Caching**: O(1) for cached lookups

### Memory Usage
- **Per Center**: ~64 bytes (position, biome pointer, climate data, ID)
- **Cache**: Bounded by center density and loaded region size
- **Typical**: ~500 centers per 10,000x10,000 block region = ~32KB

### Optimization Features
1. **Spatial Partitioning**: Grid-based lookup (only check nearby cells)
2. **On-Demand Generation**: Centers created as needed
3. **Persistent Cache**: Generated centers stored permanently
4. **Distance Quantization**: Coarse caching reduces memory

## Testing Recommendations

### Visual Tests
1. Enable Voronoi mode and generate world
2. Verify distinct biome regions with clear centers
3. Check for organic boundaries (no hexagons)
4. Compare with traditional noise mode

### Configuration Tests
1. Test different center spacings (200, 400, 800 blocks)
2. Test different blend radii (40, 80, 120 blocks)
3. Verify smooth transitions at boundaries

### Performance Tests
1. Measure biome lookup time (should be <0.1ms)
2. Check memory usage growth
3. Test cache efficiency

## Usage Example

```cpp
// Enable Voronoi mode
BiomeMap biomeMap(seed);
biomeMap.setVoronoiMode(true);

// Configure biome size
BiomeVoronoi* voronoi = biomeMap.getVoronoi();
voronoi->setCenterSpacing(600.0f);  // Large biomes
voronoi->setBlendRadius(100.0f);    // Smooth transitions

// Use as normal
auto influences = biomeMap.getBiomeInfluences(worldX, worldZ);
// Now uses Voronoi center-based selection!
```

## Future Enhancements

1. **Hierarchical Centers**: Large regions subdivided into smaller biomes
2. **Biome Chains**: Connected biomes (e.g., river systems)
3. **Elevation-Based Centers**: Mountain peaks as biome centers
4. **Dynamic Spacing**: Vary center density by region type
5. **Biome Clusters**: Group related biomes together

## Conclusion

The Voronoi center system provides a powerful alternative to noise-based biome selection:
- ✅ Natural clustering with clear centers
- ✅ Organic boundaries (no geometric artifacts)
- ✅ Configurable biome sizes
- ✅ Better exploration experience
- ✅ Performance optimized
- ✅ Backward compatible (optional feature)

This creates more memorable and navigable biome distributions while maintaining natural-looking transitions.
