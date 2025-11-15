# Multi-Layer Biome Noise System

## Overview

The Multi-Layer Biome Noise System provides fine-grained control over biome selection through a configurable, multi-dimensional noise architecture. This system enables independent control of each noise layer, allowing for diverse biome patterns ranging from massive continental-scale zones to compact, varied landscapes.

## Architecture

### Four-Dimensional Noise Space

Biome selection operates in a 4D noise space:

1. **Temperature** (0-100): Cold to hot gradient
2. **Moisture** (0-100): Dry to wet gradient
3. **Weirdness** (0-100): Normal to unusual biome combinations
4. **Erosion** (0-100): Smooth to rough terrain transitions

### Two-Layer System

Each dimension consists of two noise layers:

- **Base Layer**: Large-scale continental patterns
  - Low frequency (0.0003-0.0015 typical)
  - Creates the fundamental biome distribution
  - Spans hundreds to thousands of blocks

- **Detail Layer**: Local variations and texture
  - Higher frequency (0.002-0.025 typical)
  - Adds variety within biome regions
  - Creates natural-looking transitions

### Layer Blending

Layers are combined using a configurable blend ratio:

```
final_value = (base_layer * blend_ratio) + (detail_layer * (1 - blend_ratio))
```

- `blend_ratio = 0.7` means 70% base, 30% detail
- Higher ratio = more emphasis on large-scale patterns
- Lower ratio = more local variation and detail

## Configuration System

### BiomeNoiseConfig

The root configuration structure containing all noise parameters:

```cpp
BiomeNoise::BiomeNoiseConfig config;
config.configName = "My Custom Config";
config.description = "Custom biome distribution";

// Configure each dimension
config.temperature = temperatureDimension;
config.moisture = moistureDimension;
config.weirdness = weirdnessDimension;
config.erosion = erosionDimension;

// Selection parameters
config.primaryTolerance = 20.0f;      // Temperature/moisture matching tolerance
config.weirdnessInfluence = 0.3f;     // How much weirdness affects selection (0.0-1.0)
config.erosionInfluence = 0.15f;      // How much erosion affects selection (0.0-1.0)
```

### DimensionConfig

Configuration for a complete biome dimension (temperature, moisture, weirdness, or erosion):

```cpp
BiomeNoise::DimensionConfig dimension;
dimension.dimensionName = "Temperature";
dimension.description = "Cold to hot climate axis";
dimension.baseLayer = baseLayerConfig;      // Large-scale noise
dimension.detailLayer = detailLayerConfig;  // Local variation noise
dimension.blendRatio = 0.7f;                // 70% base, 30% detail
```

### NoiseLayerConfig

Configuration for a single noise layer:

```cpp
BiomeNoise::NoiseLayerConfig layer;
layer.name = "Temperature Base";
layer.noiseType = FastNoiseLite::NoiseType_OpenSimplex2;
layer.fractalType = FastNoiseLite::FractalType_FBm;
layer.frequency = 0.0003f;    // ~3333 block features
layer.octaves = 5;            // Number of detail layers
layer.lacunarity = 2.2f;      // Detail frequency multiplier
layer.gain = 0.55f;           // Detail amplitude
```

## Noise Parameters Explained

### Frequency

Controls the scale of noise features.

**Formula**: `feature_size ≈ 1 / frequency`

- `0.0003` → ~3333 block features (continental scale)
- `0.001` → ~1000 block features (regional scale)
- `0.005` → ~200 block features (local scale)
- `0.02` → ~50 block features (micro scale)

**Effect**:
- Lower frequency = wider, more gradual patterns
- Higher frequency = smaller, more varied patterns

### Octaves

Number of noise layers combined to create detail.

**Range**: 1-8 (typical: 3-5)

**Effect**:
- 1 octave = smooth, simple patterns
- 3-4 octaves = natural-looking complexity
- 6+ octaves = highly detailed, fractal patterns

**Performance**: Each octave doubles computation cost

### Lacunarity

Frequency multiplier for each octave.

**Range**: 1.5-3.0 (typical: 2.0-2.5)

**Effect**:
- Low (1.5-2.0) = smoother, more coherent detail
- Medium (2.0-2.5) = balanced natural detail
- High (2.5-3.0) = more chaotic, varied detail

**Formula**: `octave_frequency = base_frequency * (lacunarity ^ octave)`

### Gain

Amplitude multiplier for each octave (also called persistence).

**Range**: 0.3-0.7 (typical: 0.5-0.6)

**Effect**:
- Low (0.3-0.4) = subtle detail layers, smoother overall
- Medium (0.5-0.6) = balanced detail contribution
- High (0.6-0.7) = strong detail layers, rougher patterns

**Formula**: `octave_amplitude = base_amplitude * (gain ^ octave)`

### Noise Type

The fundamental noise algorithm used:

- **OpenSimplex2**: Smooth, organic patterns (best for most uses)
- **Perlin**: Classic smooth noise (good for terrain)
- **Cellular (Worley)**: Cell-like patterns (good for special features)
- **Value**: Simple grid-based noise

### Fractal Type

How octaves are combined:

- **FBm (Fractal Brownian Motion)**: Standard layering (most common)
- **Ridged**: Creates ridge-like patterns (good for mountains/erosion)
- **PingPong**: Creates plateau-like patterns
- **Domain Warp**: Distorts noise space for organic shapes

## Preset Configurations

### Continental Scale (Default)

**Characteristics**:
- Extra large biomes spanning 2000-3000+ blocks
- Smooth, gradual transitions
- Realistic continent-like climate zones

**Use Cases**:
- Large exploration-focused worlds
- Realistic geography simulation
- Long-distance travel gameplay

**Parameters**:
```
Temperature Base: freq=0.0003, octaves=5 (~3333 block features)
Temperature Detail: freq=0.003, octaves=3 (~333 block features)
Moisture Base: freq=0.0004, octaves=5 (~2500 block features)
Moisture Detail: freq=0.0035, octaves=3 (~285 block features)
Weirdness Base: freq=0.0003, octaves=4 (~3333 block features)
Weirdness Detail: freq=0.002, octaves=2 (~500 block features)
Erosion Base: freq=0.0004, octaves=4 (~2500 block features, Ridged)
Erosion Detail: freq=0.0025, octaves=3 (~400 block features)
```

### Regional Scale

**Characteristics**:
- Large biomes spanning 1000-2000 blocks
- Balanced transitions and variety
- Good mix of exploration and diversity

**Use Cases**:
- Balanced gameplay
- Medium-sized maps
- Moderate exploration emphasis

**Parameters**:
```
Temperature Base: freq=0.0006 (~1666 blocks)
Moisture Base: freq=0.0007 (~1428 blocks)
Weirdness Base: freq=0.0005 (~2000 blocks)
Erosion Base: freq=0.0008 (~1250 blocks)
```

### Local Scale

**Characteristics**:
- Medium biomes spanning 500-1000 blocks
- Frequent transitions
- High variety in small areas

**Use Cases**:
- Small to medium maps
- Variety-focused gameplay
- Building-focused worlds

**Parameters**:
```
Temperature Base: freq=0.0012 (~833 blocks)
Moisture Base: freq=0.0015 (~666 blocks)
Weirdness Base: freq=0.0010 (~1000 blocks)
Erosion Base: freq=0.0018 (~555 blocks)
```

### Compact Scale

**Characteristics**:
- Small biomes spanning 200-400 blocks
- Very frequent transitions
- Maximum variety in minimal space

**Use Cases**:
- Small maps
- Testing and development
- Maximum biome variety showcase

**Parameters**:
```
Temperature Base: freq=0.0025 (~400 blocks)
Moisture Base: freq=0.003 (~333 blocks)
Weirdness Base: freq=0.0020 (~500 blocks)
Erosion Base: freq=0.004 (~250 blocks)
```

## Usage Examples

### Basic: Using Presets

```cpp
// Create BiomeMap with default (continental) configuration
BiomeMap biomeMap(12345);

// OR specify a preset during construction
BiomeMap regionalMap(12345, BiomeNoise::createRegionalConfig());

// OR change preset at runtime
biomeMap.applyPreset("local");  // Switch to local scale
```

### Intermediate: Custom Configuration

```cpp
// Start with a preset and modify it
BiomeNoise::BiomeNoiseConfig config = BiomeNoise::createContinentalConfig();

// Make biomes even wider
config.temperature.baseLayer.frequency = 0.0002f;  // ~5000 block features
config.moisture.baseLayer.frequency = 0.0002f;

// Add more detail
config.temperature.detailLayer.octaves = 4;
config.moisture.detailLayer.octaves = 4;

// Increase weirdness influence for more variety
config.weirdnessInfluence = 0.5f;

// Apply the custom configuration
biomeMap.setNoiseConfig(config);
```

### Advanced: Per-Dimension Tuning

```cpp
// Create a custom dimension configuration
BiomeNoise::DimensionConfig tempConfig;
tempConfig.dimensionName = "Custom Temperature";

// Configure base layer for massive temperature zones
tempConfig.baseLayer.noiseType = FastNoiseLite::NoiseType_OpenSimplex2;
tempConfig.baseLayer.frequency = 0.0001f;  // Extra wide (~10000 blocks)
tempConfig.baseLayer.octaves = 6;          // Extra detail
tempConfig.baseLayer.lacunarity = 2.0f;
tempConfig.baseLayer.gain = 0.5f;

// Configure detail layer for local variety
tempConfig.detailLayer.noiseType = FastNoiseLite::NoiseType_Perlin;
tempConfig.detailLayer.frequency = 0.005f;  // Local variation
tempConfig.detailLayer.octaves = 3;
tempConfig.detailLayer.gain = 0.6f;

// Use more base than detail for smoother transitions
tempConfig.blendRatio = 0.8f;  // 80% base, 20% detail

// Apply only to temperature dimension (dimension 0)
biomeMap.setDimensionConfig(0, tempConfig);
```

### Expert: Per-Layer Fine-Tuning

```cpp
// Fine-tune a single noise layer
BiomeNoise::NoiseLayerConfig layer;
layer.name = "Custom Moisture Base";
layer.noiseType = FastNoiseLite::NoiseType_OpenSimplex2;
layer.fractalType = FastNoiseLite::FractalType_FBm;
layer.frequency = 0.0005f;    // ~2000 block features
layer.octaves = 5;
layer.lacunarity = 2.3f;      // Slightly more varied detail
layer.gain = 0.55f;

// Apply to moisture (1) base layer (true)
biomeMap.setLayerConfig(1, true, layer);

// Apply different settings to moisture detail layer
layer.frequency = 0.004f;
layer.octaves = 3;
biomeMap.setLayerConfig(1, false, layer);
```

## How Layers Combine for Biome Selection

### Step 1: Noise Sampling

For each world position (x, z), sample all four dimensions:

```
temperature = (tempBase * 0.7) + (tempDetail * 0.3)  // blend_ratio = 0.7
moisture = (moistBase * 0.7) + (moistDetail * 0.3)
weirdness = (weirdBase * 0.65) + (weirdDetail * 0.35)  // blend_ratio = 0.65
erosion = (erosionBase * 0.6) + (erosionDetail * 0.4)  // blend_ratio = 0.6
```

### Step 2: Biome Matching

For each biome in the registry:

1. Calculate primary distance in temp/moisture space:
   ```
   tempDist = abs(temperature - biome.temperature)
   moistureDist = abs(moisture - biome.moisture)
   primaryDist = tempDist + moistureDist
   ```

2. Early exit if near-perfect match:
   ```
   if (tempDist <= 2.0 && moistureDist <= 2.0) return biome
   ```

3. Calculate influence weight if within tolerance:
   ```
   if (primaryDist <= primaryTolerance) {
       // Base proximity weight
       proximityWeight = 1.0 - (primaryDist / (primaryTolerance * 2.0))

       // Weirdness factor (boosts unusual biomes in weird areas)
       weirdnessFactor = calculateWeirdnessFactor(weirdness, biome)

       // Erosion factor (correlates with biome age)
       erosionFactor = calculateErosionFactor(erosion, biome)

       // Rarity weight (from biome definition)
       rarityWeight = biome.rarity_weight / 50.0

       // Combined weight
       totalWeight = proximityWeight * weirdnessFactor * erosionFactor * rarityWeight
   }
   ```

### Step 3: Selection

Return the biome with the highest total weight. If no biomes are within tolerance, return the closest biome by primary distance.

## Parameter Effects on Biome Patterns

### Frequency Effects

| Frequency | Feature Size | Biome Pattern |
|-----------|--------------|---------------|
| 0.0001 | ~10000 blocks | Massive continents, very gradual changes |
| 0.0003 | ~3333 blocks | Large continental zones (default) |
| 0.001 | ~1000 blocks | Regional biome clusters |
| 0.003 | ~333 blocks | Local biome patches |
| 0.01 | ~100 blocks | Micro-scale biome mixing |

### Octave Effects

| Octaves | Appearance | Notes |
|---------|------------|-------|
| 1 | Very smooth blobs | Too simple, unrealistic |
| 2-3 | Smooth with some variation | Good for secondary layers |
| 4-5 | Natural complexity | Ideal for primary layers |
| 6-8 | Highly detailed fractals | May be too chaotic |

### Blend Ratio Effects

| Ratio | Meaning | Result |
|-------|---------|--------|
| 0.9 | 90% base | Very smooth, gradual transitions |
| 0.7 | 70% base | Balanced (default) |
| 0.5 | 50/50 | Equal influence from both layers |
| 0.3 | 30% base | More varied, detail-dominated |

## Performance Considerations

### Computational Cost

Cost per noise sample:
```
base_cost = octaves * sampling_cost
total_cost = (base_layer_cost + detail_layer_cost) * 4_dimensions
```

Typical sampling costs:
- OpenSimplex2: ~100ns per sample
- Perlin: ~80ns per sample
- Cellular: ~150ns per sample

### Caching Strategy

The BiomeMap uses multi-level caching:

1. **Biome Cache**: Quantized to 4-block resolution
   - Max size: 100,000 entries (~3-4 MB)
   - LRU eviction: removes 20% when full
   - Expected hit rate: >90%

2. **Influence Cache**: Quantized to 8-block resolution
   - Used for blending calculations
   - Cleared when configuration changes

3. **Terrain/Cave Caches**: Quantized to 2-block resolution
   - Separate caches for height and density
   - Not affected by noise reconfiguration

### Optimization Tips

1. **Use lower octaves** when possible (each octave doubles cost)
2. **Prefer OpenSimplex2 or Perlin** over Cellular for base layers
3. **Quantize lookups** to cache-friendly resolutions
4. **Limit configuration changes** during gameplay (clears caches)
5. **Batch queries** when generating chunks

## Best Practices

### For Realistic Terrain

- Use Continental or Regional presets
- Keep temperature/moisture frequencies similar but not identical
- Use higher blend ratios (0.7-0.8) for smoother transitions
- Use 4-5 octaves on base layers for natural complexity

### For Varied Gameplay

- Use Local or Compact presets
- Increase weirdness influence (0.4-0.5)
- Use lower blend ratios (0.5-0.6) for more variation
- Add more octaves to detail layers

### For Performance

- Start with Continental preset (lowest frequency)
- Use 3-4 octaves maximum
- Avoid changing configuration during gameplay
- Use caching-friendly query patterns

### For Testing/Debugging

- Use Compact preset to see all biomes quickly
- Temporarily increase weirdness influence to 1.0
- Lower all frequencies by 10× to see patterns at smaller scale
- Use single octave to isolate base patterns

## Common Patterns and Examples

### Mountain Ranges

Use erosion with ridged fractal:
```cpp
config.erosion.baseLayer.fractalType = FastNoiseLite::FractalType_Ridged;
config.erosion.baseLayer.frequency = 0.0005f;
config.erosionInfluence = 0.3f;  // Strong erosion effect
```

### Island Continents

Use cellular noise for continent shapes:
```cpp
config.moisture.baseLayer.noiseType = FastNoiseLite::NoiseType_Cellular;
config.moisture.baseLayer.frequency = 0.0002f;
```

### Striped Biomes

Use very different frequencies for temp vs moisture:
```cpp
config.temperature.baseLayer.frequency = 0.0001f;  // Wide zones
config.moisture.baseLayer.frequency = 0.002f;       // Narrow bands
```

### Chaotic Mix

Max out detail and weirdness:
```cpp
config.temperature.blendRatio = 0.4f;  // More detail influence
config.moisture.blendRatio = 0.4f;
config.weirdnessInfluence = 0.8f;      // Strong variety
```

## Troubleshooting

### Biomes Too Large

- Increase base layer frequency
- Use Regional, Local, or Compact preset
- Reduce blend ratio to add more detail variation

### Biomes Too Small

- Decrease base layer frequency
- Use Continental preset
- Increase blend ratio to emphasize large patterns

### Transitions Too Sharp

- Increase number of octaves
- Increase blend ratio
- Add more detail layer octaves
- Adjust transition profile (separate system)

### Not Enough Variety

- Increase weirdness influence
- Decrease blend ratio
- Add more octaves
- Increase detail layer frequency

### Too Much Variety (Chaotic)

- Decrease weirdness influence
- Increase blend ratio
- Reduce number of octaves
- Use Continental preset

## Integration with Other Systems

### Biome Blending

The noise system provides the foundation for smooth biome transitions:
- `getBiomeInfluences()` returns weighted biome influences
- Used by terrain generation for smooth height blending
- Used by block placement for gradual surface transitions

### Terrain Generation

- Biome's `age` property correlates with erosion noise
- Biome's `height_multiplier` amplifies terrain variation
- Continuous noise ensures smooth terrain across biome borders

### Feature Placement

- Tree density uses blended noise values
- Structure placement considers biome continuity
- Ore distribution respects biome boundaries

## Future Enhancements

Potential improvements to the system:

1. **Altitude Integration**: Modify temperature based on Y-coordinate
2. **Ocean Currents**: Adjust moisture near large water bodies
3. **Voronoi Regions**: Add cellular patterns for defined biome zones
4. **SIMD Optimization**: Vectorize noise generation for 2-4× speedup
5. **Hierarchical Caching**: Multi-tier caching for better performance
6. **Runtime Visualization**: Debug tools to visualize noise patterns
7. **Preset Editor**: GUI tool for creating custom configurations

## References

- FastNoiseLite Library: https://github.com/Auburn/FastNoiseLite
- Minecraft 1.18+ Terrain Generation: Multi-dimensional climate system
- Biome Registry System: `/home/user/voxel-engine/docs/BIOME_SYSTEM.md`
- Biome Blending: `/home/user/voxel-engine/docs/BIOME_BLENDING_DESIGN.md`

## API Reference

### Constructor

```cpp
BiomeMap(int seed);
BiomeMap(int seed, const BiomeNoise::BiomeNoiseConfig& config);
```

### Configuration Methods

```cpp
void setNoiseConfig(const BiomeNoise::BiomeNoiseConfig& config);
const BiomeNoise::BiomeNoiseConfig& getNoiseConfig() const;

void setDimensionConfig(int dimension, const BiomeNoise::DimensionConfig& config);
void setLayerConfig(int dimension, bool isBaseLayer, const BiomeNoise::NoiseLayerConfig& config);

void applyPreset(const std::string& presetName);
```

### Noise Sampling

```cpp
float getTemperatureAt(float worldX, float worldZ);  // 0-100
float getMoistureAt(float worldX, float worldZ);     // 0-100
float getWeirdnessAt(float worldX, float worldZ);    // 0-100
float getErosionAt(float worldX, float worldZ);      // 0-100
```

### Biome Selection

```cpp
const Biome* getBiomeAt(float worldX, float worldZ);
std::vector<BiomeInfluence> getBiomeInfluences(float worldX, float worldZ);
```

---

**Version**: 1.0
**Author**: Agent 22 - Biome Blending Algorithm Team
**Date**: 2025-11-15
