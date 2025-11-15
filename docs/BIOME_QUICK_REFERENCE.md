# Biome System Quick Reference

## Biome Data Structures

### Biome Struct (`include/biome_system.h`)

```cpp
struct Biome {
    // REQUIRED PROPERTIES
    std::string name;           // Biome identifier (lowercase, underscore-separated)
    int temperature;            // 0 (coldest) to 100 (warmest)
    int moisture;               // 0 (driest) to 100 (wettest)
    int age;                    // 0 (rough/mountains) to 100 (flat/plains)
    int activity;               // 0-100: structure spawn rate

    // SPAWNING AND GENERATION
    BiomeSpawnLocation spawn_location;  // Underground, AboveGround, Both
    int lowest_y;                       // Minimum Y level
    bool underwater_biome;              // Can spawn as ocean floor
    bool river_compatible;              // Can rivers cut through
    int biome_rarity_weight;            // 1-100 (higher = more common)
    std::string parent_biome;           // Parent biome for variants
    float height_multiplier;            // Terrain height multiplier

    // VEGETATION
    bool trees_spawn;                   // Can trees spawn
    int tree_density;                   // 0-100: tree spawn density
    int vegetation_density;             // 0-100: grass/flowers/mushrooms

    // BLOCKS
    std::vector<int> required_blocks;   // Must spawn
    std::vector<int> blacklisted_blocks; // Cannot spawn
    int primary_surface_block;          // Top layer block (default: grass)
    int primary_stone_block;            // Stone layer block (default: stone)
    int primary_log_block;              // Tree trunk block (-1 = default)
    int primary_leave_block;            // Tree leaves block (-1 = default)

    // STRUCTURES
    std::vector<std::string> required_structures;
    std::vector<std::string> blacklisted_structures;

    // CREATURES
    std::vector<std::string> blacklisted_creatures;
    bool hostile_spawn;                 // Can hostiles spawn naturally

    // WEATHER AND ATMOSPHERE
    std::string primary_weather;
    std::vector<std::string> blacklisted_weather;
    glm::vec3 fog_color;                // Custom fog color (RGB 0-1)
    bool has_custom_fog;

    // ORES
    std::vector<OreSpawnRate> ore_spawn_rates;

    // TREE GENERATION
    std::vector<TreeTemplate> tree_templates;  // 10 unique templates per biome
};
```

### OreSpawnRate Struct

```cpp
struct OreSpawnRate {
    std::string ore_name;   // Name/ID of ore
    float multiplier;       // 1.0 = normal, 2.0 = double, 0.5 = half
};
```

### BiomeSpawnLocation Enum

```cpp
enum class BiomeSpawnLocation {
    Underground = 1,      // Only underground
    AboveGround = 2,      // Only above ground
    Both = 3              // Both (Underground | AboveGround)
};
```

## BiomeRegistry API

### Singleton Access
```cpp
BiomeRegistry& registry = BiomeRegistry::getInstance();
```

### Loading Biomes
```cpp
// Load all .yaml files from directory
bool success = registry.loadBiomes("assets/biomes/");

// Generate tree templates (must be called after loadBiomes)
registry.generateTreeTemplates(treeGenerator);
```

### Querying Biomes
```cpp
// Get biome by name (case-insensitive)
const Biome* biome = registry.getBiome("plains");

// Get biome by index
const Biome* biome = registry.getBiomeByIndex(0);

// Get biome count
int count = registry.getBiomeCount();

// Get all biomes
const auto& biomes = registry.getAllBiomes();

// Get biomes in temperature/moisture range
auto biomes = registry.getBiomesInRange(tempMin, tempMax, moistMin, moistMax);
```

## BiomeMap API

### Creating BiomeMap
```cpp
BiomeMap biomeMap(worldSeed);
```

### Querying World Data
```cpp
// Get biome at 2D world position
const Biome* biome = biomeMap.getBiomeAt(worldX, worldZ);

// Get temperature at position (0-100)
float temp = biomeMap.getTemperatureAt(worldX, worldZ);

// Get moisture at position (0-100)
float moisture = biomeMap.getMoistureAt(worldX, worldZ);

// Get terrain height at position
int height = biomeMap.getTerrainHeightAt(worldX, worldZ);

// Get cave density at 3D position (0-1, <0.45 = cave)
float caveDensity = biomeMap.getCaveDensityAt(worldX, worldY, worldZ);

// Check if position is in underground biome chamber
bool isUnderground = biomeMap.isUndergroundBiomeAt(worldX, worldY, worldZ);
```

## All Defined Biomes

| Name | Type | Temp | Moist | Age | Trees | Rarity | Special |
|------|------|------|-------|-----|-------|--------|---------|
| Plains | Surface | 60 | 50 | 80 | 15% | 70 | Very flat |
| Forest | Surface | 55 | 60 | 65 | 75% | 55 | Dense trees |
| Desert | Surface | 90 | 5 | 75 | 0% | 45 | No trees, sand |
| Mountain | Surface | 35 | 40 | 20 | 25% | 35 | 3.5x height |
| Winter Forest | Surface | 25 | 55 | 60 | 70% | 45 | Spruce trees |
| Ice Tundra | Surface | 10 | 30 | 85 | 5% | 30 | Snow/ice |
| Tropical Rainforest | Surface | 85 | 90 | 60 | 95% | 30 | Very dense |
| Swamp | Surface | 65 | 85 | 90 | 35% | 35 | Very flat, water |
| Savanna | Surface | 80 | 30 | 85 | 12% | 50 | Sparse trees |
| Taiga | Surface | 20 | 50 | 65 | 70% | 55 | Cold forest |
| Ocean | Surface | 55 | 100 | 70 | 0% | 65 | Underwater |
| Mushroom Cave | Underground | 45 | 75 | 50 | 0% | 15 | Peaceful, Y<-100 |
| Crystal Cave | Underground | 30 | 40 | 35 | 0% | 10 | Rich ores, Y<-150 |
| Deep Dark | Underground | 15 | 20 | 25 | 0% | 8 | Very hostile, Y<-200 |

## Temperature-Moisture Zones

### Temperature Ranges
- **0-20**: Arctic/Alpine (ice tundra, high mountains)
- **20-40**: Cold (taiga, winter forest)
- **40-60**: Temperate (plains, forest)
- **60-80**: Warm (savanna, swamp)
- **80-100**: Hot (desert, tropical rainforest)

### Moisture Ranges
- **0-20**: Arid (desert, barren)
- **20-40**: Dry (savanna, grassland)
- **40-60**: Moderate (plains, forest)
- **60-80**: Humid (rainforest, swamp)
- **80-100**: Saturated (ocean, swamp, rainforest)

## Common Usage Patterns

### During World Generation
```cpp
// Initialize biome system
auto& registry = BiomeRegistry::getInstance();
registry.loadBiomes("assets/biomes/");
registry.generateTreeTemplates(treeGenerator);

// Create biome map
BiomeMap biomeMap(worldSeed);

// For each chunk position
for (int x = 0; x < CHUNK_SIZE; x++) {
    for (int z = 0; z < CHUNK_SIZE; z++) {
        float worldX = chunkX * CHUNK_SIZE + x;
        float worldZ = chunkZ * CHUNK_SIZE + z;

        // Get biome and terrain height
        const Biome* biome = biomeMap.getBiomeAt(worldX, worldZ);
        int height = biomeMap.getTerrainHeightAt(worldX, worldZ);

        // Generate terrain based on biome
        for (int y = 0; y <= height; y++) {
            if (y == height) {
                chunk.setBlock(x, y, z, biome->primary_surface_block);
            } else if (y > height - 4) {
                chunk.setBlock(x, y, z, BLOCK_DIRT);
            } else {
                chunk.setBlock(x, y, z, biome->primary_stone_block);
            }
        }

        // Check for caves
        for (int y = 0; y < height; y++) {
            float density = biomeMap.getCaveDensityAt(worldX, y, worldZ);
            if (density < 0.45f) {
                chunk.setBlock(x, y, z, BLOCK_AIR); // Cave
            }
        }
    }
}
```

### Spawning Trees
```cpp
if (biome->trees_spawn) {
    float spawnChance = biome->tree_density / 100.0f;
    if (randomFloat() < spawnChance) {
        int logBlock = biome->primary_log_block;
        int leafBlock = biome->primary_leave_block;

        if (logBlock == -1) logBlock = BLOCK_OAK_LOG;
        if (leafBlock == -1) leafBlock = BLOCK_LEAVES;

        // Use tree templates from biome
        const TreeTemplate& treeTemplate = biome->tree_templates[rand() % 10];
        spawnTree(worldX, height + 1, worldZ, treeTemplate);
    }
}
```

### Checking Ore Spawn Rates
```cpp
for (const auto& oreRate : biome->ore_spawn_rates) {
    if (oreRate.ore_name == "gold") {
        float spawnChance = baseGoldChance * oreRate.multiplier;
        // Use adjusted spawn chance...
    }
}
```

### Custom Fog
```cpp
if (biome->has_custom_fog) {
    renderer.setFogColor(biome->fog_color);
}
```

## Block IDs Reference

| ID | Block Name | Use Case |
|----|------------|----------|
| 0 | Air | Caves, sky |
| 1 | Stone | Underground |
| 2 | Dirt | Below surface |
| 3 | Grass | Surface (temperate) |
| 4 | Sand | Desert, beach |
| 5 | Water | Ocean, rivers |
| 6 | Oak Log | Temperate trees |
| 7 | Leaves | Tree foliage |
| 8 | Spruce Log | Cold biome trees |
| 9 | Spruce Leaves | Cold biome foliage |
| 10 | Snow | Arctic biomes |
| 11 | Ice | Frozen water |
| 12 | Bedrock | Bottom layer |

## Performance Notes

### Caching
- BiomeMap caches biome lookups (quantized to 4-block resolution)
- Terrain height cached at 2-block resolution
- Max cache size: 100,000 entries (~3MB)
- Thread-safe: uses shared_mutex for parallel reads

### Optimization Tips
1. Query BiomeMap once per column, not per block
2. Biome queries are cached automatically
3. FastNoiseLite is thread-safe for reads
4. Avoid frequent biome registry modifications during generation

## Adding New Biomes

1. Create `.yaml` file in `assets/biomes/`
2. Define required properties (name, temperature, moisture, age, activity)
3. Add optional properties as needed
4. Test by running the game (auto-loaded)
5. (Optional) Add constant to `include/biome_types.h`

See `docs/BIOME_SYSTEM.md` for complete documentation.
