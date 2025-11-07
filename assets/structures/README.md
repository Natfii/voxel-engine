# Structure System Documentation

## Overview

The structure system allows you to define and spawn pre-built voxel structures in the world. Structures are defined in YAML files and can have multiple variations with weighted randomization.

## File Format

Structure files must be placed in `assets/structures/` and use the `.yaml` extension.

### Basic Structure

```yaml
name: Structure Name

variations:
  - length: 5        # X dimension (blocks)
    width: 5         # Z dimension (blocks)
    height: 6        # Y dimension (blocks)
    depth: 0         # Depth below spawn point (0 = surface level)
    chance: 100      # Spawn chance percentage (0-100)
    structure:
      # Layer-by-layer definition (Y axis, bottom to top)
      - [[row1], [row2], ...]  # Layer 0
      - [[row1], [row2], ...]  # Layer 1
      # ... more layers
```

### Parameters

#### Required Fields

- **name**: Display name for the structure
- **variations**: Array of structure variations (see below)

#### Variation Fields

- **length**: Structure size along X axis (must match structure array dimensions)
- **width**: Structure size along Z axis (must match structure array dimensions)
- **height**: Structure size along Y axis (number of layers)
- **depth**: How many blocks below the spawn point to start building (0 = at surface)
- **chance**: Probability weight for this variation (0-100)
  - If multiple variations exist, their chances should sum to 100
  - Example: Two variations with chance 60 and 40 means 60% / 40% split
- **structure**: 3D array of block IDs

### Structure Array Format

The structure is defined as a 3D array with the following nesting order:

```
structure[Y][Z][X]
  Y = vertical layers (0 = bottom, increases upward)
  Z = rows (depth dimension)
  X = columns (width dimension)
```

**Example visualization for a 3x3x2 structure:**

```yaml
structure:
  # Layer 0 (bottom)
  - [[1, 1, 1],   # Z=0 (back row):   X: [0, 1, 2]
     [1, 0, 1],   # Z=1 (middle row):  X: [0, 1, 2]
     [1, 1, 1]]   # Z=2 (front row):   X: [0, 1, 2]

  # Layer 1 (top)
  - [[2, 2, 2],
     [2, 0, 2],
     [2, 2, 2]]
```

### Block IDs

Block IDs must match your block registry (defined in `assets/blocks/*.yaml`):

- **0**: Air (empty space)
- **1**: Stone
- **2**: Dirt
- **3**: Grass
- **6**: Oak Log
- **7**: Leaves
- **... (see your block YAML files for complete list)**

## Examples

### Example 1: Simple Pillar

```yaml
name: Stone Pillar

variations:
  - length: 1
    width: 1
    height: 5
    depth: 0
    chance: 100
    structure:
      - [[1]]  # Layer 0: Stone
      - [[1]]  # Layer 1: Stone
      - [[1]]  # Layer 2: Stone
      - [[1]]  # Layer 3: Stone
      - [[1]]  # Layer 4: Stone
```

### Example 2: Small House with Multiple Variations

```yaml
name: Village House

variations:
  # Standard house (60% chance)
  - length: 5
    width: 5
    height: 4
    depth: 0
    chance: 60
    structure:
      # Layer 0 (floor)
      - [[1, 1, 1, 1, 1],
         [1, 2, 2, 2, 1],
         [1, 2, 2, 2, 1],
         [1, 2, 2, 2, 1],
         [1, 1, 1, 1, 1]]
      # ... more layers ...

  # Large house (40% chance)
  - length: 7
    width: 7
    height: 5
    depth: 0
    chance: 40
    structure:
      # ... larger house definition ...
```

## Usage in Code

### Spawning a Structure

```cpp
// Get structure registry
auto& structureRegistry = StructureRegistry::instance();

// Spawn at world position
structureRegistry.spawnStructure(
    "Oak Tree",        // Structure name
    world,             // World instance
    renderer,          // Renderer for updating chunks
    10.0f, 0.0f, 5.0f  // World X, Y, Z coordinates
);
```

### Loading Structures

Structures are automatically loaded at startup from `assets/structures/`:

```cpp
// In main.cpp or initialization code
StructureRegistry::instance().loadStructuresFromDirectory("assets/structures");
```

## Design Guidelines

### Performance Considerations

1. **Keep structures reasonably sized**: Large structures (>32 blocks in any dimension) may span multiple chunks, requiring more GPU buffer updates
2. **Use air (0) for empty space**: Don't use solid blocks where not needed
3. **Limit variation count**: Too many variations increase memory usage

### Visual Quality

1. **Add variety with variations**: Multiple variations prevent repetitive worlds
2. **Use appropriate blocks**: Match block types to structure purpose (wood for houses, stone for ruins, etc.)
3. **Consider terrain integration**: Use the `depth` parameter to embed structures partially underground

### Common Patterns

1. **Trees**: Trunk (logs) with canopy (leaves), depth=0 for surface placement
2. **Buildings**: Solid foundations, hollow interiors, depth=1 for partially buried
3. **Ruins**: Partial walls with gaps (air blocks), varying heights
4. **Decorations**: Small structures (flowers, rocks, signs) with low density

## Troubleshooting

### Structure Not Appearing

1. **Check console logs**: The structure system logs loaded structures and spawn attempts
2. **Verify block IDs**: Ensure all block IDs in your structure exist in the block registry
3. **Check file location**: Must be in `assets/structures/` with `.yaml` extension
4. **Validate YAML syntax**: Use a YAML validator to check for syntax errors

### Structure Floating or Underground

- Adjust the `depth` parameter:
  - `depth: 0` - Structure base at spawn Y coordinate (surface level)
  - `depth: 1` - First layer is 1 block below spawn point
  - `depth: -1` - First layer is 1 block above spawn point

### Chunks Not Updating After Spawn

The structure system automatically updates affected chunks and their neighbors. If chunks aren't updating:

1. Ensure you pass a valid `VulkanRenderer*` to `spawnStructure()`
2. Check console for error messages
3. Verify the spawn coordinates are within loaded chunk bounds

## Advanced Topics

### Biome-Specific Structures

Currently not built-in, but you can implement biome filtering in your spawn logic:

```cpp
// Example: Only spawn trees in grass biome
if (biome == Biome::GRASS) {
    structureRegistry.spawnStructure("Oak Tree", world, renderer, x, y, z);
}
```

### Structure Rotation

Not currently supported. To add rotation:
1. Define 4 variations (one for each cardinal direction)
2. Or implement rotation in the structure loading code

### Conditional Block Placement

The current system always places blocks exactly as defined. For more advanced behavior (like "replace only air blocks"), modify `StructureRegistry::spawnStructure()` in `src/structure_system.cpp`.

## See Also

- `src/structure_system.cpp` - Structure system implementation
- `include/structure_system.h` - Structure API
- `assets/blocks/*.yaml` - Block definitions and IDs
- `docs/structure_system.md` - Additional technical documentation

---

*Created for the Voxel Engine project*
*Documentation by Claude (Anthropic AI Assistant)*
