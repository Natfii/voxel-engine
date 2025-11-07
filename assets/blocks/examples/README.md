# Custom Block Guide

This voxel engine supports **runtime block loading**, meaning you can add your own custom blocks without recompiling!

## How to Add a Custom Block

### 1. Create Your Textures
- Create 64x64 PNG texture files for your block
- Place them in the `assets/blocks/` directory
- For a simple block: one texture file (e.g., `myblock.png`)
- For a cube-mapped block: separate textures for different faces

### 2. Create a YAML Definition File
Create a `.yaml` file in `assets/blocks/` with your block definition:

**Note**: The `id` field is **optional**. See "Block ID Assignment" section below.

#### Simple Block (Same Texture on All Faces)
```yaml
id: 10  # OPTIONAL: Omit for auto-assign. Use explicit IDs for important blocks.
name: "My Custom Block"
texture: "myblock.png,1.2"  # filename,variation (1.0-2.0 range)
durability: 5
liquid: false
affected_by_gravity: false
flammability: 0
transparency: 0.0  # 0.0 = opaque, 1.0 = fully transparent
```

#### Cube-Mapped Block (Different Textures Per Face)
```yaml
id: 11
name: "My Cube Block"
cube_map:
  top: "top.png,1.5"
  bottom: "bottom.png"
  sides: "side.png"  # Or specify front/back/left/right individually
durability: 3
liquid: false
affected_by_gravity: false
flammability: 0
transparency: 0.0
```

#### Full Cube-Map Example
```yaml
id: 12
name: "Detailed Block"
cube_map:
  top: "block_top.png"
  bottom: "block_bottom.png"
  front: "block_front.png"
  back: "block_back.png"
  left: "block_left.png"
  right: "block_right.png"
durability: 4
liquid: false
affected_by_gravity: false
flammability: 0
transparency: 0.0
```

#### Color-Based Block (No Texture)
```yaml
id: 13
name: "Colored Block"
texture: "#ff5733"  # Hex color code
durability: 2
liquid: false
affected_by_gravity: false
flammability: 0
transparency: 0.0
```

### 3. Launch the Game
- Start the game normally
- Your block will automatically be loaded into the texture atlas
- Open the inventory (press **I**) to see your custom block
- It will appear with a proper isometric icon showing its textures

## Block ID Assignment System

The engine uses a **smart two-pass loading system** for block IDs:

### Pass 1: Explicit IDs (Recommended for Core Blocks)
Blocks with an `id` field in their YAML are loaded first at their specified ID:
```yaml
id: 10
name: "Stone Brick"
```

### Pass 2: Auto-Assigned IDs (For Flexible Blocks)
Blocks **without** an `id` field are automatically assigned IDs after all explicit IDs:
```yaml
# No id field - will auto-assign!
name: "Decorative Lamp"
```
- Auto-assigned IDs start at `(highest_explicit_id + 1)`
- If highest explicit ID is 5, auto-assigned blocks get IDs 6, 7, 8...
- Assignment order is filesystem-dependent (don't rely on it!)

### Best Practices
✅ **Use explicit IDs for:**
- Core blocks (stone, dirt, grass, etc.)
- Blocks used in world generation
- Blocks that must have stable IDs across game updates

✅ **Omit IDs (auto-assign) for:**
- Decorative/cosmetic blocks
- Experimental/temporary blocks
- Blocks you might remove later

⚠️ **Never change explicit IDs after worlds are created** - it will break saved worlds!

### Example: Mixing Explicit and Auto IDs
```yaml
# core_blocks/stone.yaml - Explicit ID (used in world gen)
id: 1
name: "Stone"
durability: 5

# decorative/lamp.yaml - No ID (won't affect world gen)
name: "Lamp"
durability: 2
```

## Block Properties Explained

| Property | Type | Description |
|----------|------|-------------|
| `id` | integer | **(Optional)** Explicit block ID. Omit for auto-assign starting from highest+1 |
| `name` | string | Display name shown in inventory |
| `texture` | string | Texture file or hex color (#RRGGBB) |
| `cube_map` | object | Define different textures per face |
| `durability` | integer | How hard to break (0 = instant) |
| `liquid` | boolean | Is this block a liquid? |
| `affected_by_gravity` | boolean | Does it fall like sand? |
| `flammability` | integer | How flammable (0 = fireproof) |
| `transparency` | float | 0.0-1.0, how see-through it is |
| `color` | array | RGB tint `[r, g, b]` (0.0-1.0 range) |

## Texture Variation

The `,variation` parameter adds texture variety:
- `1.0` = no variation (default)
- `1.2` = slight variation (recommended)
- `1.5` = moderate variation
- `2.0` = high variation

This prevents repetitive patterns by scaling textures slightly differently per block instance.

## Tips

- Keep texture files small (64x64 is ideal, larger will be downscaled)
- Use PNG format with alpha channel for transparency
- Test your block IDs don't conflict with existing blocks
- Restart the game after adding new blocks
- The texture atlas will automatically resize to fit all blocks

## Reserved Block IDs

These core blocks have **explicit IDs assigned**:

- **0**: Air (system reserved, cannot be changed)
- **1**: Stone
- **2**: Dirt
- **3**: Grass Block
- **4**: Sand
- **5**: Water

**Recommendation**: Start your custom explicit IDs at **10 or higher** to leave room for future core blocks!

If you use auto-assigned IDs (no `id` field), blocks will get IDs starting from 6.

## Troubleshooting

**Block doesn't appear in inventory:**
- Check your YAML syntax is valid
- Ensure texture files exist in assets/blocks/
- Check the console for loading messages (Pass 1 and Pass 2)
- Look for error messages during block loading

**Block shows as gray cube:**
- Texture file might not be loading
- Check filename matches exactly (case-sensitive on Linux)
- Verify PNG format and not corrupted

**Block has wrong/unexpected ID:**
- Check console output to see assigned ID
- **Pass 1** shows blocks with explicit IDs
- **Pass 2** shows auto-assigned blocks and their IDs
- If using auto-assign, ID = (highest explicit ID + 1 + position)
- Use explicit `id:` field for guaranteed IDs

**Block ID changed after adding new blocks:**
- This happens with auto-assigned IDs!
- Adding blocks with explicit IDs increases the starting point for auto-assigned blocks
- **Solution**: Use explicit IDs for important blocks to prevent ID changes

## Example: Adding a Cobblestone Block

1. Create `cobblestone.png` (64x64 gray rocky texture)
2. Create `cobblestone.yaml`:
```yaml
id: 10
name: "Cobblestone"
texture: "cobblestone.png,1.4"
durability: 6
liquid: false
affected_by_gravity: false
flammability: 0
transparency: 0.0
```
3. Launch game and press **I** to see it in inventory!

---

**The block icon system automatically generates isometric 3D previews of your blocks using their actual textures!**
