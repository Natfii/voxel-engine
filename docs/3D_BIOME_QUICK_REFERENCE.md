# 3D Biome Influence - Quick Reference

## Core Concepts

### Altitude-Based Temperature
```cpp
float tempDrop = biomeMap->getAltitudeTemperatureModifier(worldY);
// Y=64 (sea level): 0° drop
// Y=100: ~18° drop
// Y=130: ~33° drop
// Y=150+: ~43-60° drop (capped)
```

### Altitude Influence Factor
```cpp
float influence = biomeMap->getAltitudeInfluence(worldY, terrainHeight);
// Returns 0.0-1.0
// 0.0 = at terrain surface (no effect)
// 1.0 = 20+ blocks above terrain (full effect)
```

### Snow Coverage
```cpp
bool hasSnow = biomeMap->shouldApplySnowCover(worldX, worldY, worldZ);
// True when effective temperature < 20
// Considers: base temp, altitude cooling, noise variation
```

## Common Usage Patterns

### 1. Surface Block Selection with Altitude
```cpp
// Get base biome
const Biome* biome = biomeMap->getBiomeAt(worldX, worldZ);
int baseBlock = biome->primary_surface_block;

// Apply altitude modifications
int finalBlock = biomeMap->getAltitudeModifiedBlock(worldX, worldY, worldZ, baseBlock);
// Result: grass → stone → snow with increasing altitude
```

### 2. 3D Biome Influences
```cpp
// Get altitude-aware biome weights
auto influences = biomeMap->getBiomeInfluences3D(worldX, worldY, worldZ);

// At high altitude:
// - Cold biomes get +50% weight
// - Warm biomes get -60% weight
```

### 3. Terrain Generation Integration
```cpp
for (int y = 0; y < CHUNK_HEIGHT; y++) {
    float worldY = chunkY * CHUNK_HEIGHT + y;
    int terrainHeight = biomeMap->getTerrainHeightAt(worldX, worldZ);

    if (worldY == terrainHeight) {
        // Surface block - use altitude-modified version
        int block = biomeMap->getAltitudeModifiedBlock(worldX, worldY, worldZ, baseSurfaceBlock);
    }
}
```

## Altitude Zones

### Zone 1: Surface (0-2 blocks above terrain)
- **Behavior:** Normal biome surface blocks
- **Snow:** Applied if temperature threshold met
- **Example:** Grass, sand, dirt (with optional snow layer)

### Zone 2: Elevated (2-15 blocks above terrain)
- **Behavior:** Gradual transition to exposed rock
- **Probability:** Increases with altitude influence
- **Example:** Mixed grass/stone, becoming more rocky

### Zone 3: High Peaks (15+ blocks above terrain)
- **Behavior:** Primarily exposed stone
- **Snow:** Heavy coverage if cold enough
- **Example:** Rocky peaks with snow caps

## Performance Notes

- **Cache resolution:** 8-block quantization (3D)
- **Cache size:** 100,000 entries shared limit
- **Thread-safe:** Parallel chunk generation supported
- **Overhead:** ~1-2% with cache hits, ~5-10% on misses

## Block ID Reference
```cpp
1  = BLOCK_STONE    // Exposed rock at altitude
3  = BLOCK_GRASS    // Low altitude surface
4  = BLOCK_DIRT     // Subsurface
7  = BLOCK_SAND     // Desert/beach surface
8  = BLOCK_SNOW     // High altitude / cold regions
```

## Example Altitude Transitions

### Temperate Plains → Mountain Peak
```
Y=65:  Grass (base biome, temp ~60°)
Y=80:  Grass with occasional stone patches
Y=95:  Mixed grass/stone (temp ~45°, cooling effect)
Y=110: Mostly stone, some grass (temp ~38°)
Y=125: Stone only (temp ~30°)
Y=140: Stone with snow patches (temp ~22°)
Y=155: Snow-capped peak (temp ~15°)
```

### Desert → Alpine Snow
```
Y=70:  Sand surface (base biome, temp ~85°)
Y=100: Sand with scattered stone (temp ~70°)
Y=130: Mixed sand/stone (temp ~52°)
Y=160: Stone with snow starting (temp ~32°)
Y=190: Heavy snow coverage (temp ~17°)
```

## Tips & Best Practices

1. **Always check terrain height first** before applying altitude modifications
2. **Use getBiomeInfluences3D()** for accurate block selection at any altitude
3. **Snow coverage is probabilistic** - adds natural variation
4. **Position-based random** ensures consistency across chunks
5. **Cache is automatic** - no manual management needed

## Integration Checklist

- [ ] Load biomes with BiomeRegistry
- [ ] Initialize BiomeMap with seed
- [ ] Use getAltitudeModifiedBlock() for surface blocks
- [ ] Check shouldApplySnowCover() for snow layers
- [ ] Use getBiomeInfluences3D() for accurate blending
- [ ] Profile performance with your chunk generation

## Further Reading

- **Full Documentation:** `/home/user/voxel-engine/AGENT_30_3D_BIOME_INFLUENCE_SUMMARY.md`
- **Usage Examples:** `/home/user/voxel-engine/examples/3d_biome_usage_example.cpp`
- **Biome System:** `/home/user/voxel-engine/docs/BIOME_SYSTEM.md`
