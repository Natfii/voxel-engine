# Biome Selection Noise System

## Overview

The biome selection noise system uses a multi-dimensional noise approach inspired by Minecraft 1.18+ terrain generation to create varied, interesting, and continuous biome distributions across the world.

## Design Philosophy

### Multi-Layered Approach
The system uses **4 noise dimensions** to select biomes:
1. **Temperature** - Primary climate axis (cold to hot)
2. **Moisture** - Primary climate axis (dry to wet)
3. **Weirdness** - Creates variety and unusual biome combinations
4. **Erosion** - Influences terrain roughness and transitions

Each dimension has:
- **Base noise** (large-scale, 800-1500 block features)
- **Detail noise** (medium-scale, 70-200 block features)

### Continuous Generation
- Uses **world coordinates** (not chunk coordinates) for seamless generation
- Biomes span **multiple chunks** due to very low frequency noise
- No chunk boundary artifacts
- Thread-safe with shared caching for performance

## Noise Parameters

### Layer 1: Temperature
**Purpose**: Primary climate axis defining cold to hot zones

**Base Temperature Noise:**
- Type: OpenSimplex2
- Fractal: FBm (Fractal Brownian Motion)
- Octaves: 5
- Lacunarity: 2.2
- Gain: 0.55
- Frequency: 0.0003 (~3333 block features - EXTRA WIDE)

**Temperature Variation:**
- Type: OpenSimplex2
- Fractal: FBm
- Octaves: 3
- Frequency: 0.003 (~333 block features)

**Combination**: 70% base + 30% variation

### Layer 2: Moisture
**Purpose**: Primary climate axis defining dry to wet zones

**Base Moisture Noise:**
- Type: OpenSimplex2
- Fractal: FBm
- Octaves: 5
- Lacunarity: 2.2
- Gain: 0.55
- Frequency: 0.0004 (~2500 block features - EXTRA WIDE)

**Moisture Variation:**
- Type: OpenSimplex2
- Fractal: FBm
- Octaves: 3
- Frequency: 0.0035 (~285 block features)

**Combination**: 70% base + 30% variation

### Layer 3: Weirdness
**Purpose**: Creates unusual biome combinations and prevents monotonous patterns

**Base Weirdness Noise:**
- Type: OpenSimplex2
- Fractal: FBm
- Octaves: 4
- Lacunarity: 2.5 (more dramatic variation)
- Gain: 0.6
- Frequency: 0.0003 (~3333 block features - EXTRA WIDE)

**Weirdness Detail:**
- Type: Perlin (smoother detail)
- Fractal: FBm
- Octaves: 2
- Frequency: 0.002 (~500 block features)

**Combination**: 65% base + 35% detail

**Effects:**
- High weirdness (>60): Boosts rare/unusual biomes by 50%
- Low weirdness (<40): Favors common biomes by 30%

### Layer 4: Erosion
**Purpose**: Influences terrain roughness and biome transitions

**Base Erosion Noise:**
- Type: OpenSimplex2
- Fractal: Ridged (creates erosion-like patterns)
- Octaves: 4
- Lacunarity: 2.3
- Gain: 0.5
- Frequency: 0.0004 (~2500 block features - EXTRA WIDE)

**Erosion Detail:**
- Type: OpenSimplex2
- Fractal: FBm
- Octaves: 3
- Frequency: 0.0025 (~400 block features)

**Combination**: 60% base + 40% detail

**Effects:**
- Correlates with biome's `age` property
- 15% influence on biome selection

## Biome Selection Algorithm

### Multi-Dimensional Weighting

```
For each biome in registry:
  1. Calculate primary distance (temperature + moisture)
  2. If distance <= 2.0: Return biome (perfect match)
  3. If distance <= 20.0 (tolerance):
     a. Calculate proximity weight: 1.0 - (distance / 40.0)
     b. Apply weirdness factor (0.7-1.5x multiplier)
     c. Apply erosion factor (1.0-1.15x multiplier)
     d. Apply rarity weight (biome_rarity_weight / 50.0)
     e. Total weight = proximity × weirdness × erosion × rarity
  4. Return biome with highest total weight
```

### Tolerance and Fallback
- **Primary Tolerance**: 20.0 (in temp/moisture space)
- **Perfect Match**: Distance <= 2.0 (early exit)
- **Fallback**: If no biome within tolerance, use closest by primary distance

## Performance Optimizations

### Caching System
1. **Biome Cache**
   - Resolution: 4 blocks (quantized)
   - Size: 100,000 entries max (~3MB)
   - Eviction: LRU-style (remove 20% when full)
   - Thread-safe: Shared mutex (parallel reads)

2. **Thread Safety**
   - FastNoiseLite is thread-safe for reads
   - No mutex needed for noise sampling
   - Shared cache for all threads

### Cache Key Generation
```cpp
uint64_t key = (uint32_t(x) << 32) | uint32_t(z)
```
Handles negative coordinates correctly using memcpy.

## Biome Scale

### Feature Sizes
- **Continental zones**: 2000-3333 blocks (temperature, moisture, weirdness, erosion)
- **Local variation**: 285-500 blocks (detail noise)
- **Cache resolution**: 4 blocks (smooth enough for transitions)

### Chunk Spanning
With frequency 0.0003-0.0004 (EXTRA WIDE):
- A single biome typically spans **125-200+ chunks** (2000-3200 blocks)
- Smooth gradual transitions across hundreds of blocks
- No sudden chunk-boundary changes
- Continental-scale climate patterns

## Testing the System

### Visual Testing
Generate biome maps at different world positions:
```cpp
BiomeMap biomeMap(12345);  // seed
for (int x = 0; x < 1000; x += 10) {
    for (int z = 0; z < 1000; z += 10) {
        const Biome* biome = biomeMap.getBiomeAt(x, z);
        // Log biome->name
    }
}
```

### Noise Value Testing
Sample noise values to verify ranges:
```cpp
float temp = biomeMap.getTemperatureAt(100, 200);  // Should be 0-100
float moisture = biomeMap.getMoistureAt(100, 200);  // Should be 0-100
float weirdness = biomeMap.getWeirdnessAt(100, 200);  // Should be 0-100
float erosion = biomeMap.getErosionAt(100, 200);  // Should be 0-100
```

### Continuity Testing
Sample adjacent positions to verify smoothness:
```cpp
const Biome* b1 = biomeMap.getBiomeAt(100, 100);
const Biome* b2 = biomeMap.getBiomeAt(100, 101);
// Biomes should change gradually over hundreds of blocks
```

## Future Enhancements

1. **Biome Blending**
   - Use `getBiomeInfluences()` to get multiple biomes
   - Blend terrain, vegetation, and colors smoothly

2. **Climate Zones**
   - Add altitude-based temperature modifier
   - Ocean currents (modify moisture near water)

3. **Voronoi Regions**
   - Add cellular noise for distinct biome "cells"
   - Mix with current system for variety

4. **Performance**
   - SIMD optimization for noise generation
   - Hierarchical caching (chunk-level cache)

## Related Files

- `/home/user/voxel-engine/include/biome_map.h` - Header file
- `/home/user/voxel-engine/src/biome_map.cpp` - Implementation
- `/home/user/voxel-engine/include/FastNoiseLite.h` - Noise library
- `/home/user/voxel-engine/include/biome_system.h` - Biome definitions

## Credits

Noise system inspired by:
- Minecraft 1.18+ terrain generation (multi-dimensional noise)
- FastNoiseLite library by Jordan Peck
- Modern procedural generation techniques
