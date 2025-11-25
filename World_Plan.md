# World Terrain Generation Overhaul Plan

## Executive Summary

This document proposes changes to make terrain more natural with:
- **Less valleys** - Smoother transitions, fewer sharp dips
- **Gradual, grand mountains** - Lower frequency, gentler slopes, larger ranges
- **Wide open fields/deserts** - Flatter terrain for plains and desert biomes
- **Snow on mountain peaks** - Y-level cutoff for automatic snow placement

---

## Current Issues Analysis

### Issue 1: Too Many Valleys / Rough Terrain

**Location:** `src/biome_map.cpp:42-49` (terrain noise setup)

**Problem:** The terrain noise frequency is too high (0.015), creating frequent hills and valleys.

```cpp
// Current settings (line 49):
m_terrainNoise->SetFrequency(0.015f);  // Too high - creates 67-block features
```

**Impact:** Terrain looks choppy with frequent elevation changes instead of grand, sweeping landscapes.

---

### Issue 2: Mountains Too Steep / Not Grand Enough

**Location:** `src/biome_map.cpp:216` and `assets/biomes/mountain.yaml:22`

**Problem:** Mountains have high `height_multiplier` (3.5x) but use the same noise frequency as plains, making them tall but not wide/gradual.

```cpp
// Current height variation calculation (line 216):
float heightVariation = 20.0f - (ageNormalized * 15.0f);  // 20 to 5 blocks
// With 3.5x multiplier: mountains get 70 block variation (too steep!)
```

```yaml
# mountain.yaml line 22:
height_multiplier: 3.5   # Creates tall but steep mountains
```

---

### Issue 3: Plains/Deserts Not Flat Enough

**Location:** `assets/biomes/plains.yaml:8` and `assets/biomes/desert.yaml:8`

**Problem:** Even with high `age` values (80, 75), the terrain variation formula still allows 7-8 blocks of variation.

```cpp
// Line 216: With age=80, heightVariation = 20 - (0.8 * 15) = 8 blocks
// Plains/deserts should be nearly flat (2-3 blocks max)
```

---

### Issue 4: No Snow on Mountain Peaks

**Location:** `src/chunk.cpp:491-493`

**Problem:** Surface block is always the biome's `primary_surface_block`. Mountains use stone (ID 1), but high peaks should have snow regardless of biome temperature.

```cpp
// Current code (line 491-493):
if (depthFromSurface == 1) {
    m_blocks[x][y][z] = biome->primary_surface_block;  // No Y check!
}
```

---

## Proposed Solutions

### Solution 1: Reduce Terrain Frequency for Smoother Landscapes

**Concept:** Lower noise frequency = larger features = smoother terrain. Minecraft 1.18 uses ~0.005 for base terrain.

**Code Change (`src/biome_map.cpp:42-49`):**

```cpp
// Terrain height noise - SMOOTHER for gradual landscapes
m_terrainNoise = std::make_unique<FastNoiseLite>(seed + 200);
m_terrainNoise->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
m_terrainNoise->SetFractalType(FastNoiseLite::FractalType_FBm);
m_terrainNoise->SetFractalOctaves(6);      // Was 5 - more octaves = smoother blending
m_terrainNoise->SetFractalLacunarity(2.0f);
m_terrainNoise->SetFractalGain(0.45f);     // Was 0.5 - lower = less harsh detail
m_terrainNoise->SetFrequency(0.006f);      // Was 0.015 - MUCH lower for grand terrain
                                            // 0.006 = ~167-block features (gradual hills)
```

**Why This Works:**
- Frequency 0.015 → 0.006 = 2.5x larger terrain features
- Lower gain (0.45) reduces the influence of higher octaves (finer detail)
- More octaves (6) creates smoother blending between large features

---

### Solution 2: Add Secondary "Mountain Shape" Noise

**Concept:** Mountains need a separate, very low frequency noise that creates wide mountain *ranges* instead of just tall spikes.

**New Noise Generator (`src/biome_map.cpp`, add after line 72):**

```cpp
// Mountain range noise - creates wide, gradual mountain ranges
// Very low frequency = mountains span thousands of blocks
m_mountainRangeNoise = std::make_unique<FastNoiseLite>(seed + 500);
m_mountainRangeNoise->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
m_mountainRangeNoise->SetFractalType(FastNoiseLite::FractalType_Ridged);  // Ridged for range shapes
m_mountainRangeNoise->SetFractalOctaves(4);
m_mountainRangeNoise->SetFrequency(0.0008f);  // VERY low - mountain ranges 1000+ blocks wide
```

**Add to header (`include/biome_map.h:67`):**

```cpp
std::unique_ptr<FastNoiseLite> m_mountainRangeNoise;
```

**Modified `getTerrainHeightAt()` (`src/biome_map.cpp:200-287`):**

```cpp
int BiomeMap::getTerrainHeightAt(float worldX, float worldZ) {
    using namespace TerrainGeneration;

    // ... cache check code unchanged ...

    const Biome* biome = getBiomeAt(worldX, worldZ);
    if (!biome) return BASE_HEIGHT;

    // Base terrain noise (smoother with lower frequency)
    float noise = m_terrainNoise->GetNoise(worldX, worldZ);

    // Calculate base height variation from biome age
    float ageNormalized = biome->age / 100.0f;
    ageNormalized = std::clamp(ageNormalized - m_ageBias, 0.0f, 1.0f);

    // CHANGED: Reduced base variation for flatter terrain
    // Young terrain (age=0): variation = 12 blocks (was 20)
    // Old terrain (age=100): variation = 2 blocks (was 5)
    float heightVariation = 12.0f - (ageNormalized * 10.0f);  // Range: 12 to 2

    // NEW: For mountainous biomes, use mountain range noise for gradual slopes
    float baseHeightMultiplier = biome->height_multiplier;
    if (baseHeightMultiplier > 1.5f) {
        // Sample mountain range noise for gradual, wide mountains
        float rangeNoise = m_mountainRangeNoise->GetNoise(worldX, worldZ);
        rangeNoise = (rangeNoise + 1.0f) * 0.5f;  // Map to [0, 1]

        // Mountains only rise where range noise is high
        // This creates gradual slopes from plains → foothills → peaks
        float mountainInfluence = std::pow(rangeNoise, 1.5f);  // Power curve for gradual rise

        // Scale the height multiplier by mountain influence
        // At mountain edges: multiplier ~1.0 (normal terrain)
        // At mountain peaks: multiplier = full height_multiplier
        baseHeightMultiplier = 1.0f + (baseHeightMultiplier - 1.0f) * mountainInfluence;

        // ... existing mountain density scaling code can remain ...
    }

    heightVariation *= baseHeightMultiplier;

    // NEW: Apply power curve to flatten valleys while keeping peaks
    // Power > 1.0 pushes middle values down, creating flatter lowlands
    float normalizedNoise = (noise + 1.0f) * 0.5f;  // [0, 1]
    normalizedNoise = std::pow(normalizedNoise, 1.3f);  // Gentle flattening
    noise = normalizedNoise * 2.0f - 1.0f;  // Back to [-1, 1]

    int height = BASE_HEIGHT + static_cast<int>(noise * heightVariation);

    // ... cache storage unchanged ...

    return height;
}
```

---

### Solution 3: Increase Biome "Age" for Flatter Plains/Deserts

**Concept:** Higher age = flatter terrain. Increase age values for plains/deserts.

**File Changes:**

**`assets/biomes/plains.yaml` (line 8):**
```yaml
# Before:
age: 80                # Flat, old terrain

# After:
age: 95                # Very flat, ancient terrain
```

**`assets/biomes/desert.yaml` (line 8):**
```yaml
# Before:
age: 75                # Relatively flat with dunes

# After:
age: 92                # Very flat with gentle dunes
```

**`assets/biomes/forest.yaml` (line 8):**
```yaml
# Before:
age: 60                # Moderate terrain roughness

# After:
age: 70                # Gentler hills for forests
```

---

### Solution 4: Snow on Mountain Peaks (Y-Level Cutoff)

**Concept:** Any terrain above a certain Y-level gets snow instead of its normal surface block.

**Add Constant (`include/terrain_constants.h:36`):**

```cpp
constexpr int SNOW_LINE = 95;             ///< Y level above which snow appears
constexpr int SNOW_TRANSITION = 5;        ///< Blocks of gradual snow transition
```

**Modified Chunk Generation (`src/chunk.cpp:488-500`):**

```cpp
// Solid terrain - determine block type
int depthFromSurface = terrainHeight - worldY;

if (depthFromSurface == 1) {
    // Top layer - check for snow line first
    // NEW: Snow on high peaks regardless of biome
    if (worldY >= SNOW_LINE + SNOW_TRANSITION) {
        // Fully above snow line - always snow
        m_blocks[x][y][z] = BLOCK_SNOW;
    } else if (worldY >= SNOW_LINE) {
        // Transition zone - mix snow and normal surface
        // Use noise to create patchy snow line
        float snowNoise = biomeMap->getTerrainNoise(worldX, worldZ);  // Reuse terrain noise
        float snowChance = (worldY - SNOW_LINE) / (float)SNOW_TRANSITION;
        if (snowNoise > (1.0f - snowChance * 2.0f)) {
            m_blocks[x][y][z] = BLOCK_SNOW;
        } else {
            m_blocks[x][y][z] = biome->primary_surface_block;
        }
    } else {
        // Below snow line - use biome's normal surface block
        m_blocks[x][y][z] = biome->primary_surface_block;
    }
} else if (depthFromSurface <= TOPSOIL_DEPTH) {
    // Topsoil layer - dirt (or snow-covered dirt in cold areas)
    if (worldY >= SNOW_LINE && biome->temperature < 50) {
        m_blocks[x][y][z] = BLOCK_SNOW;  // Deeper snow layer
    } else {
        m_blocks[x][y][z] = BLOCK_DIRT;
    }
} else {
    // Deep underground - use biome's stone block
    m_blocks[x][y][z] = biome->primary_stone_block;
}
```

**Add Helper Method to BiomeMap (`include/biome_map.h:46`):**

```cpp
/**
 * Get raw terrain noise value at a world position (for snow line noise)
 */
float getTerrainNoise(float worldX, float worldZ);
```

**Implementation (`src/biome_map.cpp`):**

```cpp
float BiomeMap::getTerrainNoise(float worldX, float worldZ) {
    return m_terrainNoise->GetNoise(worldX, worldZ);
}
```

---

### Solution 5: Reduce Mountain Height Multiplier

**Concept:** Lower the height_multiplier but make mountains wider with the new range noise.

**`assets/biomes/mountain.yaml` (line 22):**

```yaml
# Before:
height_multiplier: 3.5   # 3.5x taller for dramatic mountains

# After:
height_multiplier: 2.5   # 2.5x taller - still dramatic but more gradual
age: 15                  # Slightly younger for more detail (was 20)
```

---

## Visual Comparison (Expected Results)

| Feature | Before | After |
|---------|--------|-------|
| Valley Frequency | Every 67 blocks | Every 167 blocks |
| Mountain Slope | ~45 degrees | ~20-30 degrees |
| Plains Variation | 8 blocks | 2-3 blocks |
| Desert Variation | 7 blocks | 2 blocks |
| Mountain Peak | Grass/Stone | Snow above Y=95 |
| Mountain Width | 50-100 blocks | 300-1000 blocks |

---

## Noise Parameter Summary

| Noise | Old Frequency | New Frequency | Effect |
|-------|---------------|---------------|--------|
| Terrain | 0.015 | 0.006 | 2.5x smoother |
| Mountain Range | N/A | 0.0008 | Wide mountain ranges |
| Temperature | 0.00008 | 0.00008 | Keep same (biomes OK) |
| Moisture | 0.0001 | 0.0001 | Keep same |

---

## Implementation Priority

1. **HIGH** - Reduce terrain frequency (Solution 1) - Biggest visual impact
2. **HIGH** - Snow on mountain peaks (Solution 4) - Adds realism
3. **MEDIUM** - Mountain range noise (Solution 2) - More complex but dramatic
4. **MEDIUM** - Increase biome ages (Solution 3) - Simple YAML changes
5. **LOW** - Reduce mountain multiplier (Solution 5) - Fine-tuning

---

## Files to Modify

| File | Changes |
|------|---------|
| `src/biome_map.cpp` | Noise parameters (lines 42-49, 200-287) |
| `include/biome_map.h` | Add mountain range noise member (line 67) |
| `include/terrain_constants.h` | Add SNOW_LINE constant (line 36) |
| `src/chunk.cpp` | Snow line check (lines 488-500) |
| `assets/biomes/plains.yaml` | Increase age to 95 |
| `assets/biomes/desert.yaml` | Increase age to 92 |
| `assets/biomes/forest.yaml` | Increase age to 70 |
| `assets/biomes/mountain.yaml` | Reduce height_multiplier to 2.5 |

---

## Testing Plan

1. **Generate new world** - Verify terrain is smoother
2. **Walk through plains** - Should be nearly flat with gentle rolls
3. **Find mountains** - Should rise gradually over 500+ blocks
4. **Climb to Y=100+** - Should see snow appear
5. **Check deserts** - Should be flat with minimal dunes
6. **Performance test** - Ensure new noise doesn't impact chunk gen speed

---

## References

- [Red Blob Games - Making maps with noise](https://www.redblobgames.com/maps/terrain-from-noise/) - Octave/frequency tuning
- [Minecraft Wiki - Noise Generator](https://minecraft.wiki/w/Noise_generator) - Official parameters
- [Alan Zucconi - Minecraft World Generation](https://www.alanzucconi.com/2022/06/05/minecraft-world-generation/) - 1.18 terrain
- [Job Talle - Hydraulic Erosion](https://jobtalle.com/simulating_hydraulic_erosion.html) - Natural valley formation
- [Nick's Blog - SoilMachine](https://nickmcd.me/2022/04/15/soilmachine/) - Multi-layer erosion

---

## Alternative: Quick Fix (Minimal Changes)

If you want faster results without adding new noise generators:

**Just change these 3 values:**

1. `src/biome_map.cpp:49` - Change frequency from `0.015f` to `0.008f`
2. `src/biome_map.cpp:216` - Change `20.0f` to `10.0f` (less height variation)
3. `src/chunk.cpp:491` - Add Y > 95 check for snow

This gives 80% of the visual improvement with 20% of the code changes.

