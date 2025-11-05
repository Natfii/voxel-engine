# Cube Map Texture System

## Overview

The voxel engine now supports **cube maps** - the ability to assign different textures to different faces of a block. This is essential for blocks like grass that have different appearances on top, bottom, and sides.

## Features

- **Per-face textures**: Assign different textures to each of the 6 faces of a cube
- **Flexible configuration**: Use simplified shortcuts like "sides" for all 4 horizontal faces
- **Backwards compatible**: Existing single-texture blocks still work
- **Texture variation**: All cube map textures support the texture variation feature

## YAML Format

### Simple Texture (Backwards Compatible)

```yaml
name: "Stone"
texture: "stone.png"
durability: 10
affected_by_gravity: false
```

### Cube Map with "sides" Shortcut

```yaml
name: "Grass"
cube_map:
  top: "grass.png"       # Top face (+Y)
  bottom: "dirt.png"     # Bottom face (-Y)
  sides: "grass.png"     # All 4 side faces (front, back, left, right)
durability: 3
affected_by_gravity: false
```

### Cube Map with Individual Faces

```yaml
name: "CustomBlock"
cube_map:
  top: "top_texture.png"
  bottom: "bottom_texture.png"
  front: "front_texture.png"   # -Z direction
  back: "back_texture.png"     # +Z direction
  left: "left_texture.png"     # -X direction
  right: "right_texture.png"   # +X direction
durability: 5
affected_by_gravity: false
```

### Cube Map with Fallbacks

You can mix specific faces with the "sides" fallback:

```yaml
name: "LogBlock"
cube_map:
  top: "log_top.png"       # Specific texture for top
  bottom: "log_top.png"    # Specific texture for bottom
  sides: "log_side.png"    # All 4 sides use this texture
```

Or use "all" as a universal fallback:

```yaml
name: "PartialCubeMap"
cube_map:
  all: "default.png"       # Fallback for any face not specified
  top: "special_top.png"   # Only top is different
```

## Face Orientation Reference

- **Top** (`top`): +Y direction (upward)
- **Bottom** (`bottom`): -Y direction (downward)
- **Front** (`front`): -Z direction
- **Back** (`back`): +Z direction
- **Left** (`left`): -X direction
- **Right** (`right`): +X direction

## Texture Atlas

The cube map system integrates seamlessly with the texture atlas:

1. All unique textures are loaded and resized to 64×64 pixels
2. Textures are packed into a single atlas (power-of-2 grid)
3. Each face references the appropriate position in the atlas
4. Duplicate textures are only loaded once (e.g., if multiple blocks use "dirt.png")

## Implementation Details

### Block Definition Structure

```cpp
struct BlockDefinition {
    struct FaceTexture {
        int atlasX;  // X position in atlas grid
        int atlasY;  // Y position in atlas grid
    };

    FaceTexture all;      // Default for all faces
    FaceTexture top;      // +Y face
    FaceTexture bottom;   // -Y face
    FaceTexture front;    // -Z face
    FaceTexture back;     // +Z face
    FaceTexture left;     // -X face
    FaceTexture right;    // +X face

    bool useCubeMap;      // True if using per-face textures
    // ... other properties
};
```

### Mesh Generation

During chunk mesh generation, the appropriate texture is selected for each face:

```cpp
const BlockDefinition::FaceTexture& topTex =
    def.useCubeMap ? def.top : def.all;
```

This ensures backwards compatibility - blocks without cube maps use the "all" texture for every face.

## Example Blocks

### Grass Block (Classic Minecraft Style)

```yaml
name: "Grass"
cube_map:
  top: "grass.png"
  bottom: "dirt.png"
  sides: "grass_side.png"  # Dirt texture with grass on top edge
texture_variation: 1.5
durability: 3
affected_by_gravity: false
```

### Log Block (Tree Trunk)

```yaml
name: "Log"
cube_map:
  top: "log_top.png"      # Tree ring pattern
  bottom: "log_top.png"   # Tree ring pattern
  sides: "log_bark.png"   # Bark texture
durability: 8
affected_by_gravity: false
```

### Crate/Box Block

```yaml
name: "Crate"
cube_map:
  top: "crate_top.png"
  bottom: "crate_bottom.png"
  sides: "crate_side.png"
durability: 6
affected_by_gravity: false
```

### Directional Block (Different on Each Face)

```yaml
name: "Furnace"
cube_map:
  top: "furnace_top.png"
  bottom: "furnace_bottom.png"
  front: "furnace_front.png"   # With opening
  back: "furnace_back.png"
  left: "furnace_side.png"
  right: "furnace_side.png"
durability: 15
affected_by_gravity: false
```

## Migration Guide

To convert existing blocks to use cube maps:

1. **No changes needed** for uniform blocks (stone, dirt, sand) - they work as-is
2. **For grass-like blocks**: Convert from single texture to cube_map format
3. **Keep texture_variation**: It works with all cube map textures

### Before (Single Texture)

```yaml
name: "Grass"
texture: "grass.png"
durability: 3
```

### After (Cube Map)

```yaml
name: "Grass"
cube_map:
  top: "grass.png"
  bottom: "dirt.png"
  sides: "grass.png"
durability: 3
```

## Performance

The cube map system has minimal performance impact:

- **Texture atlas**: All textures packed into one GPU texture (same as before)
- **Face selection**: Happens during mesh generation (CPU-side, one-time)
- **Runtime**: No additional overhead - same vertex data structure
- **Memory**: Slightly larger BlockDefinition structure (negligible)

## Texture Variation

Cube maps fully support texture variation (random zoom/offset per block):

```yaml
cube_map:
  top: "grass.png"
  bottom: "dirt.png"
  sides: "grass.png"
texture_variation: 1.5  # Applies to all faces
```

Each face gets a random portion of its texture based on the block's world position.

## Future Enhancements

Potential additions to the cube map system:

1. **Per-face texture variation**: Different zoom factors for different faces
2. **Texture rotation**: Rotate textures for specific faces
3. **Animated textures**: Frame-based animation support
4. **Normal maps**: Per-face normal mapping
5. **Block states**: Different cube maps based on block state (e.g., furnace on/off)

## Troubleshooting

### Textures not loading

- Verify texture files exist in `assets/blocks/`
- Check console output for texture loading messages
- Ensure texture filenames match exactly (case-sensitive)

### Wrong texture on faces

- Check face names in YAML (top, bottom, front, back, left, right, sides, all)
- Verify "sides" vs individual face specifications
- Review console output during atlas building

### Missing textures appear as solid color

- Blocks with failed texture loads fall back to solid color
- Check the `color` field or texture file paths
