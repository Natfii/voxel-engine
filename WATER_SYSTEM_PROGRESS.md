# Water System Progress Log
**Date:** 2025-11-07
**Session Focus:** Water rendering, physics, and animation improvements

---

## Summary
Implemented complete animated water system with proper transparency rendering, flow animation, and natural swimming physics. Water now works like Minecraft with diagonal flowing animation and intuitive controls.

---

## 1. Animated Water Texture (✅ Complete)

### Implementation
- **File:** `shaders/shader.frag` (lines 22-45)
- **Method:** Shader-based UV scrolling within atlas cell (industry standard)
- **No multiple texture frames needed** - animation happens purely in shader

### Technical Details
```glsl
// Detects transparent blocks (water) by alpha < 0.99
// Scrolls UVs within the 0.25x0.25 atlas cell
float scrollSpeed = 40.0;  // Fast, noticeable flow
localUV.y += time * scrollSpeed;      // Downward flow
localUV.x += time * scrollSpeed * 0.6; // Diagonal drift (60% of vertical)
```

### Benefits
- ✅ Zero CPU overhead (all GPU)
- ✅ Seamless tiling with fract() wrapping
- ✅ Stays within atlas boundaries
- ✅ Adjustable speed via `scrollSpeed` parameter
- ✅ Diagonal flow for realistic water movement

### Configuration
- Speed: `40.0` (2x faster than initial 20.0)
- Diagonal ratio: `0.6` (60% horizontal vs vertical)
- Animation source: `ubo.skyTimeData.x` (time of day 0-1)

---

## 2. Two-Pass Transparent Rendering (✅ Complete)

### Problem Solved
Water was rendering with dark artifacts due to depth sorting issues. Transparent blocks need special handling.

### Implementation

#### A. Separate Geometry Buffers (`chunk.h/cpp`)
```cpp
// Opaque geometry
VkBuffer m_vertexBuffer, m_indexBuffer;
uint32_t m_vertexCount, m_indexCount;

// Transparent geometry
VkBuffer m_transparentVertexBuffer, m_transparentIndexBuffer;
uint32_t m_transparentVertexCount, m_transparentIndexCount;
```

#### B. Mesh Generation Split (`chunk.cpp::generateMesh()`)
- Separate vertex/index vectors for opaque vs transparent
- Route faces based on `block.transparency > 0.0f`
- Modified `renderFace` lambda to accept `useTransparent` flag

#### C. Two-Pass Rendering (`world.cpp::renderWorld()`)

**Pass 1: Opaque Geometry**
- Render all solid blocks with depth writes enabled
- Skip chunks with only transparent geometry

**Pass 2: Transparent Geometry**
- Switch to transparent pipeline (depth writes OFF)
- Sort chunks back-to-front by distance from camera
- **CRITICAL:** Rebind descriptor sets (contains texture atlas)
- Render transparent chunks in sorted order

```cpp
// Bind transparent pipeline
vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                 renderer->getTransparentPipeline());

// MUST rebind descriptor sets!
VkDescriptorSet descriptorSet = renderer->getCurrentDescriptorSet();
vkCmdBindDescriptorSets(commandBuffer, ...);

// Sort back-to-front
std::sort(transparentChunks.begin(), transparentChunks.end(),
         [](const auto& a, const auto& b) { return a.second > b.second; });
```

#### D. Transparent Pipeline (`vulkan_renderer.cpp`)
- Identical to opaque pipeline except:
  - `depthTestEnable = VK_TRUE` (still test depth)
  - `depthWriteEnable = VK_FALSE` (don't write to depth buffer)

---

## 3. Water Block Configuration (✅ Complete)

### File: `assets/blocks/water.yaml`
```yaml
id: 5
name: "Water"
texture: "water.png"
# animated_tiles: 2  # DISABLED - atlas allocation needs refactoring
durability: 0
liquid: true
affected_by_gravity: false
flammability: 0
transparency: 0.25  # 25% transparent
color: [1.5, 1.8, 2.0]  # Brighten the dark blue water texture
```

### Key Properties
- `liquid: true` - Enables swimming physics
- `transparency: 0.25` - Routes to transparent rendering pass
- `color: [1.5, 1.8, 2.0]` - Brightens dark water texture
- `affected_by_gravity: false` - Water stays in place

---

## 4. Face Culling Fix (✅ Complete)

### Problem
Water bottom face wasn't visible when looking up from below.

### Solution (`chunk.cpp`)
```cpp
if (isCurrentLiquid) {
    // Render against both air AND solid blocks (not just air)
    shouldRender = !neighborIsLiquid;
} else {
    // Solid blocks only render against air
    shouldRender = !neighborIsSolid;
}
```

Now you can see water from underneath!

---

## 5. Swimming Physics Improvements (✅ Complete)

### A. Water Detection - Head Instead of Feet (`player.cpp:197-213`)

**Old (Buggy):** Checked center/eye position
**New (Fixed):** Checks head position (90% of player height)

```cpp
glm::vec3 playerFeet = Position - glm::vec3(0.0f, PLAYER_EYE_HEIGHT, 0.0f);
glm::vec3 headPos = playerFeet + glm::vec3(0.0f, PLAYER_HEIGHT * 0.9f, 0.0f);
int blockID = world->getBlockAt(headPos.x, headPos.y, headPos.z);
```

**Why This Matters:**
- Head above water → Not swimming → Collision enabled → Can walk on land
- Head in water → Swimming → Collision disabled → Free movement through water
- Fixes collision issues when underwater near blocks

### B. Disabled Collision When Swimming (`player.cpp:478, 489`)

```cpp
// Skip collision when swimming - allow free movement through water
if (!m_inLiquid && checkHorizontalCollision(testPos, world)) {
    movement.x = 0.0f;
    m_velocity.x = 0.0f;
}
```

No more getting pushed out of water by blocks!

### C. Powerful Jump in Water (`player.cpp:224-226`)

**Old:** `SWIM_SPEED * 0.8f` = 1.2 units/sec (too weak)
**New:** `JUMP_VELOCITY * 0.6f` = 2.52 units/sec (over 2x stronger)

```cpp
if (m_inLiquid) {
    // Strong jump in water to allow jumping out when at surface
    m_velocity.y = JUMP_VELOCITY * 0.6f;  // 60% of normal jump
}
```

Now you can jump out of water onto land!

---

## 6. Player Spawn Fix (✅ Complete)

### Problem
Player spawning underwater or inside terrain.

### Solution (`main.cpp:197-202`)
- Increased clearance from 5 blocks to **10 blocks** above terrain
- Raised minimum spawn height from 35.0 to **40.0** world units
- Accounts for water layers on surface

```cpp
float spawnY = (terrainHeight + 10) * 0.5f + 0.8f;  // terrain + 10 blocks clearance
if (spawnY < 40.0f) {
    spawnY = 40.0f;  // Minimum safe height
}
```

---

## Technical Notes & Lessons Learned

### Atlas Animation Limitation (Known Issue)
- Attempted 2x2 atlas allocation for multi-frame animation
- **Problem:** Consecutive indices (5,6,7,8) don't form 2D grid in 4x4 atlas
- They span across rows instead of forming a square block
- **Solution Used:** Single-frame with UV scrolling (works perfectly)
- **Future:** Would need non-consecutive allocation or texture arrays

### Critical Gotchas
1. **Descriptor sets MUST be rebound** when switching pipelines
2. **Sort transparent geometry back-to-front** for correct blending
3. **Water detection at head** prevents collision bugs underwater
4. **Variable name conflicts** - watch for `feetPos` redefinition across scopes

### Performance
- No performance impact from animation (GPU-only)
- Two-pass rendering is standard practice, negligible cost
- Chunk sorting is O(n log n) but n is small (visible chunks only)

---

## Files Modified

### Shaders
- `shaders/shader.frag` - Water animation UV scrolling

### Headers
- `include/chunk.h` - Transparent buffer members, getTransparentVertexCount()
- `include/world.h` - Updated renderWorld() signature
- `include/vulkan_renderer.h` - Transparent pipeline member and getter

### Source
- `src/chunk.cpp` - Dual mesh generation, separate buffers, face culling
- `src/world.cpp` - Two-pass rendering with sorting
- `src/vulkan_renderer.cpp` - Transparent pipeline creation
- `src/player.cpp` - Swimming physics, water detection, jumping
- `src/main.cpp` - Spawn height fixes

### Assets
- `assets/blocks/water.yaml` - Transparency, color, liquid properties

---

## Testing Checklist

- [x] Water renders with transparency (no dark artifacts)
- [x] Water animates with diagonal flow
- [x] Water visible from all angles (including below)
- [x] Can swim through water without collision issues
- [x] Can jump out of water onto land
- [x] Player spawns above water/terrain
- [x] No texture atlas bleeding (water shows correct texture)
- [x] Performance is smooth (60 FPS maintained)

---

## Known Issues / Future Work

### Animation
- [x] ~~Water animation disabled~~ → FIXED with UV scrolling
- [x] ~~Animation scrolls entire atlas~~ → FIXED with cell bounds
- [ ] Multi-frame animation needs proper 2D atlas allocation (low priority)

### Physics
- [x] ~~Block push-out when swimming~~ → FIXED
- [x] ~~Can't jump out of water~~ → FIXED
- [x] ~~Water detection at feet causes collision bugs~~ → FIXED (now checks head)
- [ ] Could add water current/flow direction (future enhancement)

### Spawn
- [x] ~~Spawning underwater~~ → FIXED
- [x] ~~Spawning inside terrain~~ → FIXED

---

## How Water Works Now (Summary)

1. **Rendering:** Two-pass system (opaque first, then transparent back-to-front)
2. **Animation:** GPU-based UV scrolling at 40x speed with diagonal drift
3. **Physics:** Head-based detection, no collision when swimming, powerful jumps
4. **Transparency:** 25% transparent with proper depth handling
5. **Visual:** Brightened color multiplier, visible from all angles

**Result:** Minecraft-style water that looks good and feels natural to interact with!

---

## Commands to Build & Test

```bash
# Compile shaders
glslc shaders/shader.frag -o build/Release/shaders/frag.spv
glslc shaders/shader.vert -o build/Release/shaders/vert.spv

# Build project
cmake --build build --config Release

# Run
./build/Release/voxel-engine.exe
```

---

## Next Session Ideas

1. **Lava blocks** - Reuse water system with different texture/color
2. **Water source/flow mechanics** - Minecraft-style spreading
3. **Underwater fog** - Reduce visibility when submerged
4. **Swimming particles** - Splash effects at surface
5. **Drowning mechanic** - Air meter when underwater
6. **Water surface waves** - Vertex displacement in shader
7. **Reflections** - Screen-space or planar reflections

---

**Status:** Water system is complete and production-ready! 🌊✨
