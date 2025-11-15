# Biome Blending - Quick Reference Guide
**Agent 10 - Biome Blending Research Team**

---

## TL;DR - Key Decisions

| Parameter | Value | Rationale |
|-----------|-------|-----------|
| **Recommended Biome Scale** | 800-1500 blocks (keep current) | Already optimal, matches modern expectations |
| **Transition Zone Width** | 80-120 blocks | ~10% of biome diameter, natural feel |
| **Blending Algorithm** | Distance-weighted interpolation | Industry standard, smooth results |
| **Search Radius** | 25 units (temp/moisture space) | Captures 2-4 nearby biomes |
| **Blend Distance** | 15 units | Start of smooth falloff |
| **Performance Impact** | +20% worst case, +10% typical | Acceptable with caching |
| **Cache Resolution** | 8 blocks (influences), 2 blocks (height) | Balance between quality and memory |

---

## Algorithm Overview (One Page)

### 1. Biome Influence Calculation
```
For each position (x, z):
  ├─ Get temperature & moisture from noise
  ├─ Find all biomes within search radius (25 units)
  ├─ Calculate distance to each biome in temp/moisture space
  ├─ Apply smooth falloff function (exponential)
  ├─ Normalize weights to sum to 1.0
  └─ Return weighted biome list
```

### 2. Terrain Height Blending
```
For each column:
  ├─ Get biome influences
  ├─ Sample base terrain noise (shared)
  ├─ For each biome:
  │   ├─ Calculate height variation (based on age + multiplier)
  │   ├─ Apply to base noise
  │   └─ Accumulate weighted height
  └─ Return rounded average height
```

### 3. Block Selection (Probabilistic)
```
For surface block:
  ├─ Get biome influences
  ├─ Create weighted pool of surface blocks
  ├─ Use deterministic RNG (seeded by position)
  ├─ Select block proportional to influence weights
  └─ Return selected block ID
```

---

## Key Code Changes

### BiomeMap.h - New Methods
```cpp
// Main blending API
std::vector<BiomeInfluence> getBiomeInfluences(float worldX, float worldZ);
int getBlendedTerrainHeight(float worldX, float worldZ);
int getBlendedSurfaceBlock(float worldX, float worldZ, uint32_t seed);
float getBlendedTreeDensity(float worldX, float worldZ);
glm::vec3 getBlendedFogColor(float worldX, float worldZ);
```

### Chunk.cpp - Generation Changes
```cpp
// OLD:
const Biome* biome = biomeMap->getBiomeAt(worldX, worldZ);
int terrainHeight = biomeMap->getTerrainHeightAt(worldX, worldZ);
m_blocks[x][y][z] = biome->primary_surface_block;

// NEW:
auto influences = biomeMap->getBiomeInfluences(worldX, worldZ);
int terrainHeight = biomeMap->getBlendedTerrainHeight(worldX, worldZ);
uint32_t seed = hash(worldX, worldZ, m_seed);
m_blocks[x][y][z] = biomeMap->getBlendedSurfaceBlock(worldX, worldZ, seed);
```

---

## Falloff Function (Critical Math)

```cpp
float calculateInfluenceWeight(float distance, float rarityWeight) {
    const float BLEND_DISTANCE = 15.0f;
    const float SEARCH_RADIUS = 25.0f;

    if (distance > SEARCH_RADIUS) return 0.0f;

    float weight;
    if (distance <= BLEND_DISTANCE) {
        // Inner zone: linear falloff
        weight = 1.0f - (distance / BLEND_DISTANCE);
    } else {
        // Outer zone: smooth exponential decay
        float falloffDist = distance - BLEND_DISTANCE;
        float falloffRange = SEARCH_RADIUS - BLEND_DISTANCE;
        float normalized = falloffDist / falloffRange;
        weight = exp(-3.0f * normalized * normalized);
    }

    // Apply rarity modifier
    weight *= (rarityWeight / 50.0f);

    return weight;
}
```

**Graph of falloff:**
```
1.0 |█████████░░░░░░░░·····
    |          ░░░░░░·····
0.5 |            ░░░·····
    |              ·····
0.0 |________________·····
    0    5   10  15  20  25
         ← distance →
```

---

## Configuration Tuning Guide

### Too Much Blending (Biomes Lose Identity)
```cpp
SEARCH_RADIUS = 20.0f;      // Reduce from 25.0
BLEND_DISTANCE = 12.0f;     // Reduce from 15.0
SINGLE_BIOME_THRESHOLD = 0.90f;  // Reduce from 0.95
```

### Too Little Blending (Still See Hard Lines)
```cpp
SEARCH_RADIUS = 30.0f;      // Increase from 25.0
BLEND_DISTANCE = 18.0f;     // Increase from 15.0
MIN_INFLUENCE = 0.005f;     // Reduce from 0.01
```

### Performance Too Slow
```cpp
INFLUENCE_CACHE_RES = 16.0f;     // Increase from 8.0
MAX_BIOMES_PER_BLEND = 3;        // Reduce from 4
SINGLE_BIOME_THRESHOLD = 0.98f;  // Increase (skip more blending)
```

### Need More Detail
```cpp
INFLUENCE_CACHE_RES = 4.0f;      // Reduce from 8.0
HEIGHT_CACHE_RES = 1.0f;         // Reduce from 2.0
```

---

## Testing Checklist

### Visual Tests
- [ ] Walk through desert → forest transition (80 blocks, smooth gradient)
- [ ] Check mountain → plains slope (no cliffs)
- [ ] Verify 3-biome intersection (corner where 3 biomes meet)
- [ ] Ocean → land transition (beach-like gradual rise)
- [ ] Ice biome → temperate (water/ice gradual change)

### Performance Tests
- [ ] Generate 1000 chunks, measure time (<30ms average)
- [ ] Monitor memory after 1 hour playtime (<50MB cache)
- [ ] Fly through biomes at high speed (maintain 60 FPS)
- [ ] Parallel chunk generation still works

### Edge Cases
- [ ] Three biomes meeting at single point
- [ ] Extreme temperature difference (ice + desert)
- [ ] Very tall mountains next to ocean
- [ ] Single biome world (no nearby biomes to blend)

---

## Common Pitfalls to Avoid

### ❌ DON'T: Blend incompatible biomes equally
```cpp
// BAD: Sand + snow with equal weight looks wrong
if (desert_weight == 0.5 && ice_weight == 0.5) {
    // This creates weird striped patterns
}
```
**FIX:** Use temperature/moisture compatibility check

### ❌ DON'T: Ignore terrain height discontinuities
```cpp
// BAD: Mountain (height=110) next to ocean (height=50)
// Creates 60-block cliff at transition
```
**FIX:** Blend heights over wider range for extreme differences

### ❌ DON'T: Cache at too fine resolution
```cpp
// BAD: Cache every single block
cacheResolution = 1.0f;  // 32x32 chunk = 1024 cache entries!
```
**FIX:** Use 4-8 block resolution for smooth results

### ❌ DON'T: Use global RNG for block selection
```cpp
// BAD: Non-deterministic, changes on reload
block = random_choice(biomes);
```
**FIX:** Use position-seeded RNG for deterministic results

---

## Performance Optimization Tips

### 1. Early Exit for Dominant Biomes
```cpp
if (maxInfluence > 0.95f) {
    return fastpath_SingleBiome(dominantBiome);
}
// Saves ~40% of blending calculations
```

### 2. Limit Biome Count
```cpp
// Only blend top N most influential biomes
std::partial_sort(influences.begin(),
                  influences.begin() + 4,
                  influences.end());
influences.resize(4);
```

### 3. Coarse Influence Cache
```cpp
// Cache influences at 8-block resolution
// Interpolate between cache points for smooth transitions
const float COARSE_CACHE_RES = 8.0f;
```

### 4. Batch Noise Lookups
```cpp
// Request multiple noise values at once (SIMD potential)
std::vector<float> temps = getTemperatureBatch(positions);
```

---

## Integration Timeline

### Week 1: Core Infrastructure
- Day 1-2: Implement `getBiomeInfluences()`
- Day 3-4: Implement `getBlendedTerrainHeight()`
- Day 5: Testing and visual verification

### Week 2: Block & Feature Blending
- Day 1-2: Implement `getBlendedSurfaceBlock()`
- Day 3: Integrate with `Chunk::generate()`
- Day 4-5: Feature blending (trees, vegetation)

### Week 3: Optimization & Polish
- Day 1-2: Caching implementation
- Day 3: Performance profiling
- Day 4-5: Bug fixes and edge cases

---

## Debug Visualization Ideas

### 1. Biome Influence Heatmap
```cpp
// Render each biome's influence as colored overlay
void debugDrawInfluences(worldX, worldZ) {
    auto influences = getBiomeInfluences(worldX, worldZ);
    for (auto& [biome, weight] : influences) {
        drawColoredQuad(worldX, worldZ, biome.debugColor, weight);
    }
}
```

### 2. Transition Zone Markers
```cpp
// Draw lines where blending starts/ends
if (maxInfluence < 0.95f && maxInfluence > 0.5f) {
    drawDebugLine(worldX, worldZ);  // Transition zone
}
```

### 3. Height Delta Visualization
```cpp
// Show terrain height changes as gradient
float heightDelta = abs(getBlendedHeight(x,z) - getBlendedHeight(x+1,z));
drawGradient(x, z, heightDelta);  // Red = steep, green = flat
```

---

## FAQ

**Q: Should we blend underground biomes too?**
A: Yes, but use separate parameters. Underground transitions can be sharper (underground chambers are naturally more isolated).

**Q: What about biomes with very different heights (ocean + mountain)?**
A: Extend blend distance proportional to height difference. Use formula: `blendWidth = baseWidth * (1 + heightDelta/50)`.

**Q: How to handle opposite temperature biomes meeting?**
A: Add temperature compatibility check. Reduce influence weight if temp difference > 50 units.

**Q: Should tree type blend or switch abruptly?**
A: Switch abruptly based on dominant biome, but blend tree density smoothly.

**Q: Memory usage concerns?**
A: With 50K cache entries at ~64 bytes each = ~3MB. Totally acceptable.

---

## Success Metrics

### Before Blending (Current)
- ❌ Visible biome borders (straight lines)
- ❌ Height discontinuities at borders
- ❌ Instant block type changes
- ⚠️ Chunk gen: 15-25ms
- ✅ Low memory usage

### After Blending (Target)
- ✅ Smooth, natural transitions
- ✅ Gradual height changes
- ✅ Realistic block mixing
- ⚠️ Chunk gen: 18-30ms (+20%)
- ✅ Moderate memory (+2MB cache)

**Overall:** Trade minor performance cost for major visual quality improvement.

---

## Quick Reference: File Locations

```
Code Changes:
  - include/biome_map.h          (new methods)
  - src/biome_map.cpp            (blending implementation)
  - src/chunk.cpp                (use blended values)
  - src/world.cpp                (tree density blending)

Documentation:
  - docs/BIOME_BLENDING_DESIGN.md              (full spec)
  - docs/BIOME_BLENDING_QUICK_REFERENCE.md     (this file)

Testing:
  - tests/biome_blending_test.cpp              (unit tests)
```

---

**End of Quick Reference**

For full details, see: `docs/BIOME_BLENDING_DESIGN.md`
