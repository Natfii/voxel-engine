# Biome System Documentation

This document describes all available properties for biome definition files (`.yaml` files in `assets/biomes/`).

## File Format

Biome files are YAML format and must have a `.yaml` or `.yml` extension. They are loaded at runtime before world generation begins.

## Naming Convention

- Biome names should be all lowercase
- Spaces should be replaced with underscores (`_`)
- Example: `tropical_rainforest` not `Tropical Rainforest`

---

## Required Properties

These properties **must** be present in every biome file:

### `name`
**Type:** String
**Description:** The unique name of the biome. Will be converted to lowercase with spaces replaced by underscores.
**Example:** `"Desert"` → `"desert"`

### `temperature`
**Type:** Integer (0-100)
**Description:** How hot the biome is.
- `0` = Coldest (arctic/frozen)
- `50` = Temperate
- `100` = Hottest (volcanic/desert)

Blocks, creatures, and weather events can specify temperature ranges where they prefer to spawn.

### `moisture`
**Type:** Integer (0-100)
**Description:** How much water is in the ground and air.
- `0` = Very dry (desert)
- `50` = Moderate
- `100` = Very moist (swamp/rainforest)

Blocks, creatures, and weather events can specify moisture ranges where they prefer to spawn.

### `age`
**Type:** Integer (0-100)
**Description:** The geological age/erosion of the land.
- `0` = Young terrain with very uneven, rough ground (mountains, canyons)
- `50` = Moderate terrain
- `100` = Old terrain with flat, plains-like surfaces

Used to determine terrain roughness and for parent biome variations.

### `activity`
**Type:** Integer (0-100)
**Description:** How frequently structures, dens, and towns spawn.
- `0` = No structures spawn
- `50` = Moderate structure spawning
- `100` = Very high structure spawn rate

Used as a probability percentage and for parent biome variations.

---

## Optional Properties

These properties can be omitted. If not specified, sensible defaults will be used.

---

### Spawning and Generation

#### `spawn_location`
**Type:** String (`"Underground"`, `"AboveGround"`, or `"Both"`)
**Default:** `"AboveGround"`
**Description:** Bitmask for where the biome can spawn.
- `"Underground"` - Only in underground caverns
- `"AboveGround"` - Only on the surface
- `"Both"` - Can spawn in both locations

**Example:**
```yaml
spawn_location: "Underground"
```

#### `lowest_y`
**Type:** Integer
**Default:** `0`
**Description:** The lowest Y-level where this biome can spawn. Useful for underground biomes or preventing biomes from generating too deep.

**Example:**
```yaml
lowest_y: -64  # Can spawn 64 blocks below sea level
```

#### `underwater_biome`
**Type:** Boolean
**Default:** `false`
**Description:** Whether this biome can spawn as an ocean floor biome (underwater).

**Example:**
```yaml
underwater_biome: true
```

#### `river_compatible`
**Type:** Boolean
**Default:** `true`
**Description:** Whether rivers can cut through this biome during world generation.

**Example:**
```yaml
river_compatible: false  # No rivers in this biome
```

#### `biome_rarity_weight`
**Type:** Integer (1-100)
**Default:** `50`
**Description:** How common/rare the biome is during world generation.
- `1` = Extremely rare
- `50` = Average frequency
- `100` = Very common

Higher values = more likely to spawn.

**Example:**
```yaml
biome_rarity_weight: 5  # Very rare biome
```

#### `parent_biome`
**Type:** String
**Default:** None
**Description:** Name of the parent biome. Creates biome variants based on age and activity levels.

For example, "Forest Hills" could be a variant of "Forest" with different age/activity values.

**Example:**
```yaml
parent_biome: "forest"
```

#### `height_multiplier`
**Type:** Float
**Default:** `1.0`
**Description:** Multiplier applied to terrain height variation for dramatic terrain effects.
- `0.5` = Very flat terrain (half normal variation)
- `1.0` = Normal terrain height
- `2.0` = Double height mountains
- `3.5` = Extreme mountains (e.g., Mountain biome)

Used to create dramatic terrain differences without changing the age parameter.

**Example:**
```yaml
height_multiplier: 3.5  # Tall mountains
```

---

### Vegetation

#### `trees_spawn`
**Type:** Boolean
**Default:** `true`
**Description:** Whether trees can spawn in this biome at all.

Set to `false` for deserts, tundra, or other treeless biomes.

**Example:**
```yaml
trees_spawn: false  # No trees (desert)
```

#### `tree_density`
**Type:** Integer (0-100)
**Default:** `50`
**Description:** How densely trees spawn when `trees_spawn` is `true`.
- `0` = No trees
- `20` = Sparse (plains)
- `50` = Moderate
- `80` = Dense (forest)
- `100` = Very dense (jungle)

**Example:**
```yaml
tree_density: 85  # Dense forest
```

#### `vegetation_density`
**Type:** Integer (0-100)
**Default:** `50`
**Description:** Spawn rate for ground vegetation (grass, flowers, mushrooms, etc.).
- `0` = Barren ground
- `50` = Moderate vegetation
- `100` = Very lush

**Example:**
```yaml
vegetation_density: 10  # Sparse vegetation (desert)
```

---

### Block Lists

#### `required_blocks`
**Type:** Comma-separated list of block IDs
**Default:** None
**Description:** Block IDs that **must** spawn in this biome.

**Example:**
```yaml
required_blocks: "12,34,56"  # Block IDs 12, 34, and 56 must spawn
```

#### `blacklisted_blocks`
**Type:** Comma-separated list of block IDs
**Default:** None
**Description:** Block IDs that **cannot** spawn naturally in this biome.

**Example:**
```yaml
blacklisted_blocks: "5,10"  # Water and lava cannot spawn
```

---

### Structure Lists

#### `required_structures`
**Type:** Comma-separated list of structure names
**Default:** None
**Description:** Structures that **must** spawn at least once in this biome.

**Example:**
```yaml
required_structures: "desert_temple,oasis"
```

#### `blacklisted_structures`
**Type:** Comma-separated list of structure names
**Default:** None
**Description:** Structures that **cannot** spawn in this biome.

**Example:**
```yaml
blacklisted_structures: "village,mansion"
```

---

### Creature Control

#### `blacklisted_creatures`
**Type:** Comma-separated list of creature names
**Default:** None
**Description:** Creatures that **cannot** spawn naturally in this biome.

**Example:**
```yaml
blacklisted_creatures: "zombie,skeleton"
```

#### `hostile_spawn`
**Type:** Boolean
**Default:** `true`
**Description:** Whether hostile creatures can spawn naturally in this biome.

Set to `false` for peaceful biomes like mushroom islands.

**Example:**
```yaml
hostile_spawn: false  # Peaceful biome
```

---

### Primary Blocks

These define the default blocks used for terrain generation in this biome.

#### `primary_surface_block`
**Type:** Integer (block ID)
**Default:** `3` (Grass)
**Description:** The block used for the top layer of terrain.

**Example:**
```yaml
primary_surface_block: 12  # Sand for desert
```

#### `primary_stone_block`
**Type:** Integer (block ID)
**Default:** `1` (Stone)
**Description:** The block used for the stone layer beneath the surface.

**Example:**
```yaml
primary_stone_block: 1  # Standard stone
```

#### `primary_log_block`
**Type:** Integer (block ID)
**Default:** `-1` (Use default oak log)
**Description:** The block used for tree trunks in this biome.

Set to `-1` to use the default log type.

**Example:**
```yaml
primary_log_block: 15  # Birch logs
```

#### `primary_leave_block`
**Type:** Integer (block ID)
**Default:** `-1` (Use default leaves)
**Description:** The block used for tree leaves in this biome.

Set to `-1` to use the default leaf type.

**Example:**
```yaml
primary_leave_block: 18  # Jungle leaves
```

---

### Weather and Atmosphere

#### `primary_weather`
**Type:** String
**Default:** None
**Description:** The primary/most common weather type in this biome.

**Example:**
```yaml
primary_weather: "rain"
```

#### `blacklisted_weather`
**Type:** Comma-separated list of weather names
**Default:** None
**Description:** Weather types that **cannot** occur in this biome.

**Example:**
```yaml
blacklisted_weather: "snow,hail"  # Never snows in desert
```

#### `fog_color`
**Type:** RGB color string (format: "R,G,B" where each is 0-255)
**Default:** None
**Description:** Custom fog color override for this biome.

If not specified, default fog color is used.

**Example:**
```yaml
fog_color: "255,200,150"  # Orange-ish fog for desert
```

---

### Ore Distribution

#### `ore_spawn_rates`
**Type:** Comma-separated list of `ore_name:multiplier` pairs
**Default:** None
**Description:** Spawn rate multipliers for specific ores.
- `1.0` = Normal spawn rate
- `2.0` = Double spawn rate
- `0.5` = Half spawn rate
- `0.0` = No spawning

**Example:**
```yaml
ore_spawn_rates: "coal:1.5,iron:2.0,gold:0.5,diamond:0.1"
```
This means:
- Coal spawns 50% more frequently
- Iron spawns twice as often
- Gold spawns half as often
- Diamonds are very rare (10% normal rate)

---

## Complete Example: Desert Biome

```yaml
# Required properties
name: "Desert"
temperature: 85
moisture: 10
age: 70
activity: 30

# Vegetation
trees_spawn: false
tree_density: 0
vegetation_density: 5

# Spawning
spawn_location: "AboveGround"
lowest_y: 60
biome_rarity_weight: 40
river_compatible: true

# Primary blocks
primary_surface_block: 12  # Sand
primary_stone_block: 24    # Sandstone

# Structures
required_structures: "desert_temple"
blacklisted_structures: "village"

# Weather
primary_weather: "clear"
blacklisted_weather: "rain,snow"
fog_color: "255,220,180"  # Warm sandy haze

# Ore distribution
ore_spawn_rates: "coal:0.8,iron:0.7,gold:1.2"
```

---

## Complete Example: Forest Biome

```yaml
# Required properties
name: "Forest"
temperature: 60
moisture: 65
age: 50
activity: 50

# Vegetation
trees_spawn: true
tree_density: 75
vegetation_density: 80

# Spawning
spawn_location: "AboveGround"
lowest_y: 60
biome_rarity_weight: 60
river_compatible: true

# Primary blocks
primary_surface_block: 3   # Grass
primary_stone_block: 1     # Stone
primary_log_block: 17      # Oak log
primary_leave_block: 18    # Oak leaves

# Creatures
hostile_spawn: true

# Weather
primary_weather: "rain"

# Ore distribution (normal rates)
ore_spawn_rates: "coal:1.0,iron:1.0,gold:1.0"
```

---

## Currently Defined Biomes

The voxel engine currently includes the following biomes:

### Surface Biomes

1. **Plains** (Temp: 60, Moisture: 50)
   - Flat, grassy terrain with scattered trees
   - Common biome, good for settlements
   - File: `assets/biomes/plains.yaml`

2. **Forest** (Temp: 55, Moisture: 60)
   - Dense oak trees, moderate terrain
   - Good for wood gathering
   - File: `assets/biomes/forest.yaml`

3. **Desert** (Temp: 90, Moisture: 5)
   - Hot, dry, sandy terrain
   - No trees, minimal vegetation
   - File: `assets/biomes/desert.yaml`

4. **Mountain** (Temp: 35, Moisture: 40)
   - Tall, rocky terrain (3.5x height multiplier)
   - Sparse vegetation, rich in ores
   - File: `assets/biomes/mountain.yaml`

5. **Winter Forest** (Temp: 25, Moisture: 55)
   - Snowy forest with spruce trees
   - Cold climate variant
   - File: `assets/biomes/winter_forest.yaml`

6. **Ice Tundra** (Temp: 10, Moisture: 30)
   - Frozen wasteland with snow and ice
   - Very cold, minimal vegetation
   - File: `assets/biomes/ice_tundra.yaml`

7. **Tropical Rainforest** (Temp: 85, Moisture: 90)
   - Hot, wet, extremely dense vegetation
   - Maximum tree density (95%)
   - File: `assets/biomes/tropical_rainforest.yaml`

8. **Swamp** (Temp: 65, Moisture: 85)
   - Wet, flat terrain with standing water
   - Moderate tree density, murky fog
   - File: `assets/biomes/swamp.yaml`

9. **Savanna** (Temp: 80, Moisture: 30)
   - Hot, dry grassland
   - Very sparse acacia trees
   - File: `assets/biomes/savanna.yaml`

10. **Taiga** (Temp: 20, Moisture: 50)
    - Cold boreal forest with spruce trees
    - Dense forest, cold climate
    - File: `assets/biomes/taiga.yaml`

11. **Ocean** (Temp: 55, Moisture: 100)
    - Deep water biome with underwater terrain
    - Sand ocean floor
    - File: `assets/biomes/ocean.yaml`

### Underground Biomes

12. **Mushroom Cave** (Temp: 45, Moisture: 75)
    - Peaceful underground biome with fungi
    - No hostile spawns, unique flora
    - Found at Y: -100 and below
    - File: `assets/biomes/mushroom_cave.yaml`

13. **Crystal Cave** (Temp: 30, Moisture: 40)
    - Glowing crystal formations
    - Very rare, very rich in ores (2-3x multipliers)
    - Found at Y: -150 and below
    - File: `assets/biomes/crystal_cave.yaml`

14. **Deep Dark** (Temp: 15, Moisture: 20)
    - Dangerous deep underground biome
    - Very hostile, extremely rich in ores (4x gold)
    - Found at Y: -200 and below
    - File: `assets/biomes/deep_dark.yaml`

---

## Temperature-Moisture Matrix

Biomes are selected based on their position in the temperature-moisture matrix. This creates natural transitions between biomes:

```
                    MOISTURE →
         0-20      20-40     40-60     60-80     80-100
       (Arid)     (Dry)   (Moderate) (Humid)  (Saturated)
    ┌─────────┬─────────┬─────────┬─────────┬─────────┐
  0 │ Ice     │ Ice     │ Tundra  │ Tundra  │ Ice     │
 20 │ Tundra  │ Tundra  │ Winter  │ Taiga   │ Ocean   │
    │         │         │ Forest  │         │         │
T 40 ├─────────┼─────────┼─────────┼─────────┼─────────┤
E 60 │ Desert  │ Savanna │ Plains  │ Forest  │ Swamp   │
M    │         │         │         │         │         │
P 80 ├─────────┼─────────┼─────────┼─────────┼─────────┤
 100│ Desert  │ Savanna │ Savanna │ Tropical│ Tropical│
    │         │         │         │ Forest  │ Forest  │
    └─────────┴─────────┴─────────┴─────────┴─────────┘
```

**Temperature Zones:**
- 0-20: Arctic/Alpine (ice, tundra, high mountains)
- 20-40: Cold (taiga, winter forest)
- 40-60: Temperate (plains, forests, hills)
- 60-80: Warm (savanna, warm forests)
- 80-100: Hot (desert, tropical rainforest)

**Moisture Zones:**
- 0-20: Arid (desert, barren)
- 20-40: Dry (savanna, dry grassland)
- 40-60: Moderate (plains, forests)
- 60-80: Humid (rainforest, swamp)
- 80-100: Saturated (ocean, swamp, rainforest)

**Special Considerations:**
- Mountains use low `age` values (rough terrain) rather than temperature
- Underground biomes use `spawn_location: "Underground"`
- Ocean biomes use `underwater_biome: true`

---

## Adding New Biomes

### Step 1: Design Your Biome

Determine the biome's characteristics:
1. **Temperature & Moisture**: Where does it fit in the matrix?
2. **Terrain**: Young/rough (low age) or old/flat (high age)?
3. **Vegetation**: Trees? Grass? Density?
4. **Rarity**: How common should it be?
5. **Special Features**: Unique blocks, structures, or atmosphere?

### Step 2: Create YAML File

Create a new `.yaml` file in `assets/biomes/`:

```yaml
# Example: Bamboo Forest Biome

# Required properties
name: "Bamboo Forest"
temperature: 70        # Warm
moisture: 75           # Humid
age: 65                # Moderate hills
activity: 50           # Moderate structures

# Vegetation
trees_spawn: true
tree_density: 90       # Dense bamboo
vegetation_density: 70

# Spawning
spawn_location: "AboveGround"
lowest_y: 62
biome_rarity_weight: 25  # Uncommon
river_compatible: true
underwater_biome: false
height_multiplier: 1.0

# Primary blocks
primary_surface_block: 3   # Grass
primary_stone_block: 1     # Stone
primary_log_block: 6       # Oak (or add bamboo block)
primary_leave_block: 7     # Leaves

# Creatures
hostile_spawn: true

# Weather
primary_weather: "rain"
blacklisted_weather: "snow"

# Atmosphere
fog_color: "150,200,150"   # Greenish

# Ore distribution
ore_spawn_rates: "coal:1.0,iron:1.0,gold:1.1"
```

### Step 3: Test Your Biome

1. Place the YAML file in `assets/biomes/`
2. Run the game - biomes are loaded automatically
3. Check console output for loading errors
4. Explore the world to find your biome
5. Verify it looks and behaves as expected

### Step 4: (Optional) Add Code Reference

For easy code access, add a constant in `include/biome_types.h`:

```cpp
namespace BiomeTypes::Names {
    constexpr const char* BAMBOO_FOREST = "bamboo_forest";
}
```

Then in code:
```cpp
const Biome* biome = registry.getBiome(BiomeTypes::Names::BAMBOO_FOREST);
```

---

## Tips for Biome Creation

1. **Temperature and Moisture** create natural biome transitions:
   - Hot + Dry = Desert
   - Hot + Wet = Jungle
   - Cold + Dry = Tundra
   - Cold + Wet = Taiga

2. **Age and Activity** can create biome variants:
   - Forest (age: 50, activity: 50)
   - Forest Hills (age: 30, activity: 50) - more rough terrain
   - Forest Clearing (age: 50, activity: 20) - fewer structures

3. **Biome Rarity** controls world variety:
   - Common biomes: 60-80
   - Uncommon: 30-50
   - Rare: 10-25
   - Very rare: 1-9

4. **Ore Multipliers** add gameplay variety:
   - Mountain biomes could have more ores (1.5-2.0x)
   - Ocean biomes could have fewer ores (0.3-0.5x)

5. **Block IDs** must match your block registry - check `assets/blocks/` for valid IDs
