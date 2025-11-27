# Voxel Engine: Complete Handbook

**Version:** 2.3
**Last Updated:** 2025-11-27
**Status:** Production Ready (Event System + Engine API + YAML Scripting + Editor Tools)

---

## ⚠️ IMPORTANT NOTE FOR AI ASSISTANTS

**THIS HANDBOOK IS GOSPEL** - Always read `docs/ENGINE_HANDBOOK.md` before making changes:
- ✅ **READ THIS FIRST** before implementing features or fixes
- ✅ **UPDATE THIS** whenever making significant changes
- ✅ **KEEP ACCURATE** - this is the authoritative source of truth
- ✅ **VERSION HISTORY** - document all major changes in "Recent Updates"

If you're Claude Code or another AI assistant working on this project:
1. Start by reading this handbook
2. Understand the existing architecture before proposing changes
3. Update this handbook when you add/modify features
4. Keep the "Recent Updates" section current

---

## Table of Contents

1. [Introduction](#introduction)
2. [Quick Start Guide](#quick-start-guide)
3. [Core Systems](#core-systems)
4. [Architecture & Design](#architecture--design)
5. [Development Guide](#development-guide)
6. [API Reference](#api-reference)
7. [Performance & Optimization](#performance--optimization)
8. [Troubleshooting](#troubleshooting)

---

# 1. Introduction

## Overview

A modern voxel-based game engine built with **Vulkan**, featuring procedural terrain generation, infinite world streaming, dynamic lighting, and advanced rendering techniques. The engine provides a complete framework for voxel-based games with Minecraft-inspired mechanics and optimizations.

## Key Features

### Graphics & Rendering
- **Vulkan 1.0** - Modern graphics API with high performance
- **Dual Pipeline Architecture** - Separate pipelines for voxels and arbitrary meshes
- **PBR Mesh Rendering** - Cook-Torrance BRDF with metallic-roughness workflow
- **Mesh Instancing** - Efficient rendering of repeated meshes with per-instance transforms
- **OBJ Mesh Loading** - Import custom 3D models from Wavefront OBJ files
- **Procedural Mesh Generation** - Built-in generators for cube, sphere, cylinder, plane
- **Greedy Meshing** - 50-80% vertex reduction optimization for voxel terrain
- **Intelligent Face Culling** - Handles opaque, transparent, and liquid blocks correctly
- **GPU Upload Batching** - 10-15x reduction in sync points
- **Mesh Buffer Pooling** - 40-60% speedup in mesh generation
- **Heightmap-Based Lighting** - O(1) sky light calculation, 100x+ faster than BFS propagation
- **Dual-Channel Lighting** - Separate sky light (heightmap) and block light (BFS for torches/lava)
- **Dynamic Lighting** - Time-based natural lighting with smooth transitions
- **Cube Map Textures** - Different textures per block face
- **Texture Atlas System** - All textures packed into single GPU texture

### World Systems
- **Infinite Terrain** - Chunk-based streaming with priority loading
- **LOD Tier System** - FULL, MESH_ONLY, TERRAIN_ONLY tiers based on distance
- **Spawn Anchor** - Minecraft-style spawn chunks that never unload (13×13×13 cube)
- **Procedural Generation** - FastNoise-based terrain with multiple biomes
- **Biome System** - YAML-configured biomes with auto-scaling noise for even distribution
- **Structure Generation** - Trees, buildings, and custom structures
- **Water Simulation** - Cellular automata with flow dynamics, source blocks maintain level
- **Save/Load System V2** - Chunk persistence with RLE compression, RAM caching, and biome biases

### Sky & Atmosphere
- **Dual Cube Map Sky** - Natural blue day sky and star-filled night sky
- **Minecraft-Compatible Time** - 24000 ticks = 20 minutes
- **Procedural Sun & Moon** - Square voxel aesthetic with dreamy gradients
- **Baked Star Field** - Real-time twinkling shader (red, blue, white stars)
- **Dynamic Fog** - Time-based fog color and density
- **Ambient Lighting** - World brightness changes with time of day

### Player & Interaction
- **First-Person Controls** - WASD movement with sprint and jump
- **Physics-Based Movement** - Gravity, collision, swimming
- **Noclip Mode** - Fly through walls for debugging
- **Block Targeting** - Unified targeting system with rich block info
- **Block Breaking/Placing** - With proper collision and physics

### Developer Tools
- **Source Engine-Style Console** - F9 to open, Tab autocomplete
- **ConVar System** - Persistent console variables
- **Debug Overlays** - FPS, position, target info
- **Markdown Viewer** - In-game documentation viewer

### Editor Tools
- **3D Skeletal Annotation Editor** - Annotate character models with animation skeletons
  - **Console Command:** `3deditor [model_path]`
  - **Model Support:** glTF and OBJ formats
  - **Bone Placement Wizard:** Step-by-step guided workflow
  - **Interactive Gizmos:** ImGuizmo 3D translation controls
  - **Undo/Redo:** Full command history support
  - **Required Bones:** spine_root, spine_tip, leg_L, leg_R, arm_L, arm_R, head
  - **Optional Bones:** tail_base, tail_tip (for tailed characters)
  - **Camera Controls:** Orbit with middle-mouse, pan with Shift+middle-mouse, zoom with scroll
  - **Bone Hierarchy:** Visual skeleton structure display
  - **Rig Files:** YAML format saved to `assets/rigs/`
  - **Animation Preview:** Real-time preview of procedural animations (walk, idle, tail sway)

- **2D Particle Effect Editor** - Create and edit particle effects
  - **Console Command:** `particaleditor [effect_path]`
  - **Multi-Emitter:** Support for multiple emitters per effect
  - **Emitter Shapes:** Point, cone, box, and circle emission patterns
  - **Property Curves:** Size and color over particle lifetime
  - **Burst Emission:** Configurable burst patterns
  - **Physics:** Gravity and drag simulation
  - **Blend Modes:** Alpha, additive, and premultiplied alpha blending
  - **Real-Time Preview:** Live viewport with particle simulation
  - **Timeline Controls:** Play, pause, reset, and scrubbing
  - **Effect Files:** YAML format saved to `assets/particles/`
  - **Hot-Reload:** Changes apply immediately to preview

## Technical Specifications

**Graphics API:** Vulkan 1.0
**Language:** C++17
**Math Library:** GLM
**Window/Input:** GLFW 3.4
**UI Framework:** Dear ImGui 1.91.9b
**Noise Generation:** FastNoiseLite
**Configuration:** yaml-cpp

**Platforms:** Windows 10/11 (64-bit), Linux

## System Requirements

**Minimum:**
- Windows 10/11 (64-bit) or Linux
- GPU with Vulkan 1.0+ support
- 4GB RAM
- Graphics drivers updated to latest

**Supported GPUs:**
- NVIDIA GeForce 600 series or newer
- AMD Radeon HD 7000 series or newer
- Intel HD Graphics 4000 or newer

---

# 2. Quick Start Guide

## Installation & Building

### Windows

**Prerequisites:**
- **Visual Studio**:
  2017/2019/2022 (2022 Community recommended - free download)
  - Must include "Desktop development with C++" workload
  - Download: https://visualstudio.microsoft.com/downloads/

- **CMake**:
  3.10+ (minimum), 3.21+ (recommended), 3.29+ (latest)
  - Download: https://cmake.org/download/
  - Make sure to select "Add CMake to system PATH" during installation

- **Vulkan SDK**:
  1.2+ required
  - Download: https://vulkan.lunarg.com/sdk/home#windows
  - **IMPORTANT**: Restart computer after installation!

**Enhanced Build System:**

The build script automatically detects your toolchain and provides helpful guidance:

```cmd
# Clone repository
git clone <repository-url>
cd voxel-engine

# Build (auto-detects CMake, Visual Studio, Vulkan SDK)
build.bat

# Clean build (if you encounter issues)
build.bat -clean

# Show help
build.bat -help

# Run the game
run.bat
```

**Build System Features:**
- ✅ Auto-detects Visual Studio 2017/2019/2022 (prefers latest)
- ✅ Auto-detects CMake in PATH or common install locations
- ✅ Provides download links if tools are missing
- ✅ Helpful error messages with troubleshooting tips
- ✅ Backward compatible with older toolchains
- ✅ Compiles shaders automatically
- ✅ Configures and builds project in one step

**Supported Configurations:**
| Tool | Minimum | Recommended | Latest |
|------|---------|-------------|--------|
| CMake | 3.10+ | 3.21+ | 3.29+ |
| Visual Studio | 2017 | 2019 | 2022 |
| C++ Standard | C++17 | C++17 | C++20 (future) |

**Note on Visual Studio Locations:**
- VS 2022: `C:\Program Files\Microsoft Visual Studio\2022\` (64-bit)
- VS 2019/2017: `C:\Program Files (x86)\Microsoft Visual Studio\{year}\` (x86)

The build script automatically checks both locations!

### Linux

**Prerequisites:**
```bash
# Ubuntu/Debian
sudo apt install vulkan-tools libvulkan-dev vulkan-validationlayers-dev \
                 libglfw3-dev glslang-tools cmake g++

# Arch Linux
sudo pacman -S vulkan-devel glfw shaderc cmake gcc
```

**Build Steps:**
```bash
# Clone repository
git clone <repository-url>
cd voxel-engine

# Build and run
chmod +x build.sh run.sh
./build.sh
./run.sh
```

## First Run

1. **Launch the game** - The executable will be in `build/Release/` (Windows) or `build/` (Linux)
2. **Wait for world generation** - Initial spawn area loads (~10 seconds for 729 chunks)
3. **Start exploring** - Use WASD to move, mouse to look around
4. **Open console** - Press F9 to access developer console
5. **Check documentation** 

## Basic Controls

| Key | Action |
|-----|--------|
| **W/A/S/D** | Move forward/left/backward/right |
| **Mouse** | Look around |
| **Space** | Jump (or swim up in water) |
| **Shift** | Sprint (hold) / Swim down |
| **N** | Toggle noclip mode |
| **F9** | Open developer console |
| **ESC** | Pause menu / Close console |
| **Left Click** | Break block |
| **Right Click** | Place block/structure (creative) |

## Configuration

Edit `config.ini` to customize:

```ini
[World]
seed = 1124

[Window]
width = 1920
height = 1080

[Controls]
mouse_sensitivity = 0.002
```

## Essential Console Commands

```
help                  # List all commands
noclip                # Toggle flying through walls
skytime 0.5           # Set time to noon
timespeed 10          # 10x faster time
debug drawfps         # Show FPS counter
tp 0 100 0            # Teleport to coordinates
spawnstructure house  # Spawn a structure
```

---

# 3. Core Systems

## 3.1 World Generation

### Chunk System

The world is divided into **32×32×32 voxel chunks**. Each chunk contains:
- **192 KB block data** (128 KB blocks + 32 KB metadata + 32 KB lighting)
- **Mesh buffers** for GPU rendering
- **State machine** for lifecycle tracking

**Chunk Coordinates:**
- World position (x, y, z) → Chunk coordinate (x/32, y/32, z/32)
- Block index within chunk: `blockID = blocks[x][y][z]`

**Chunk State Machine (2025-11-25):**
Chunks track their lifecycle via `ChunkState` enum for thread-safe coordination:

```
UNLOADED → LOADING → GENERATED → DECORATING → AWAITING_MESH
                                                    ↓
           ACTIVE ← UPLOADING ← AWAITING_UPLOAD ← MESHING
             ↓
         UNLOADING → UNLOADED (returned to pool)
```

States:
- `UNLOADED` - In pool or not created
- `LOADING` - Terrain generation (worker thread)
- `GENERATED` - Terrain complete, awaiting decoration
- `DECORATING` - Tree/structure placement
- `AWAITING_MESH` - Ready for mesh generation
- `MESHING` - Mesh worker processing
- `AWAITING_UPLOAD` - Mesh ready for GPU
- `UPLOADING` - GPU transfer in progress
- `ACTIVE` - Fully renderable
- `UNLOADING` - Being saved/cleaned up

**Thread-Safe Methods:**
- `tryTransition(expected, new)` - Atomic CAS for worker competition
- `transitionTo(state)` - Validated transition with logging

**Chunk Pooling:**
Chunks are reused via `acquireChunk()`/`releaseChunk()` in World class:
- ~100x faster than new/delete
- Preserves vector capacities for mesh data
- `reset()` clears blocks and resets state to UNLOADED

### Biome System

Biomes are defined in YAML files in `assets/biomes/`. Each biome specifies:

**Required Properties:**
- `name` - Unique biome identifier
- `temperature` (0-100) - Heat level
- `moisture` (0-100) - Water availability
- `age` (0-100) - Geological erosion (0=mountains, 100=plains)
- `activity` (0-100) - Structure spawn frequency

**Optional Properties:**
- `spawn_location` - "Underground", "AboveGround", or "Both"
- `lowest_y` - Minimum spawn height
- `underwater_biome` - Can spawn as ocean floor
- `surface_block` - Top layer block type
- `sub_surface_block` - Second layer block
- `primary_tree_block` - Tree trunk material
- `primary_leave_block` - Tree foliage material

**Example Biome:**
```yaml
name: "Forest"
temperature: 55
moisture: 60
age: 40
activity: 70
surface_block: "grass"
sub_surface_block: "dirt"
primary_tree_block: "oak_log"
primary_leave_block: "oak_leaves"
```

**Biome Noise Range Optimization:**

The engine automatically scales noise generation to match your biome ranges:

```cpp
// At registry load time, track actual temp/moisture ranges
m_minTemperature = 5   (from coldest biome)
m_maxTemperature = 90  (from hottest biome)
m_minMoisture = 5      (from driest biome)
m_maxMoisture = 70     (from wettest biome)

// Noise maps to actual ranges instead of 0-100
temperature = mapNoiseToRange(noise, m_minTemperature, m_maxTemperature);
moisture = mapNoiseToRange(noise, m_minMoisture, m_maxMoisture);
```

**Benefits:**
- All biomes have equal representation in the world
- No "dead zones" with no matching biomes
- Better biome variety and exploration
- Console logs actual ranges: `"Biome map initialized (temp: 5-90, moisture: 5-70)"`

**Before optimization:** If biomes use temp:5-90 but noise maps to 0-100, ~10% of world has no matching biomes (falls back to closest match).

**After optimization:** Noise perfectly covers all biome ranges, every biome appears evenly distributed.

### Terrain Generation Pipeline

1. **Noise Generation** - FastNoiseLite generates height maps
2. **Biome Selection** - Temperature/moisture determines biome
3. **Terrain Shaping** - Apply biome-specific modifications
4. **Block Placement** - Fill chunk with appropriate blocks
5. **Decoration** - Place trees, structures, water
6. **Lighting Calculation** - Calculate light propagation
7. **Mesh Generation** - Create renderable geometry

### Structure Generation

Structures are defined in `assets/structures/` as YAML files:

```yaml
name: "simple_house"
size: [7, 5, 7]
blocks:
  - [0, 0, 0, "stone"]
  - [0, 0, 1, "stone"]
  # ... more blocks
```

**Console Command:**
```
spawnstructure simple_house
```

### Decoration System

**Deferred Decoration:**
Chunks require neighboring chunks to be loaded before decoration (prevents trees cutting off at chunk boundaries). If neighbors aren't ready, chunks are added to a pending queue.

**Processing Queue:**
```cpp
// main.cpp game loop
if (decorationRetryTimer >= 0.02f) {  // Check every 20ms (50 times/sec)
    world.processPendingDecorations(&renderer, 100);  // Process up to 100 chunks
    // Max throughput: 5,000 chunks/second
}
```

**Workflow:**
1. Chunk generates terrain → check if neighbors loaded
2. If neighbors ready → decorate immediately → mesh → upload
3. If neighbors missing → render terrain only, add to pending queue
4. Pending queue processes continuously (5,000 chunks/sec max)
5. When neighbors load → decorate → regenerate mesh → re-upload

**Benefits:**
- No tree cutoffs at chunk boundaries
- Deterministic decoration (same seed = same trees)
- Fast processing prevents "invisible tree" artifacts
- Chunks still render quickly (terrain first, trees within ~20ms)

## 3.2 Rendering System

### Vulkan Pipeline

**Initialization:**
1. Create Vulkan instance and select physical device
2. Create logical device and graphics queue
3. Create swapchain for presentation
4. Load and compile shaders (SPIR-V)
5. Create graphics pipeline
6. Set up command buffers

**Frame Rendering:**
1. Acquire swapchain image
2. Record command buffer
3. Submit to graphics queue
4. Present image to screen

**Optimizations:**
- **GPU Upload Batching** - Batch all chunk uploads per frame
- **Mesh Buffer Pooling** - Reuse vertex/index buffers
- **Face Culling** - Don't render hidden faces (see below)
- **Frustum Culling** - Don't render chunks outside view

### GPU Warm-Up System

**Problem:** Spawn/load radius mismatch causes chunk uploads during gameplay, creating GPU stalls.

**Initial Issue:** Spawn radius=2 (125 chunks) but load radius=5 (461 chunks needed).
Streaming system queued 336 additional chunks immediately after game start, causing:
- 300+ chunks uploading during gameplay (1 per frame)
- Each upload uses same graphics queue as rendering
- `vkWaitForFences` stalls 400-600ms waiting for queue to drain
- Result: 1.7-2.3 FPS for first 5+ minutes

**Solution 1:** Increase spawn radius to match load radius (radius=4, 729 chunks)
**Solution 2:** GPU warm-up phase - wait for all uploads during loading screen

**Implementation:**
```cpp
// After creating all GPU buffers (loading screen at 85%)
world.createBuffers(&renderer);

// NEW: GPU warm-up phase (loading screen at 87%)
loadingMessage = "Warming up GPU (this ensures smooth 60 FPS)";
renderer.waitForGPUIdle();  // Blocks until GPU finishes all uploads
```

**How it works:**
1. All spawn chunks are generated and uploaded to GPU buffers
2. `vkDeviceWaitIdle()` blocks CPU until GPU finishes processing
3. GPU deletion queue drains naturally during wait
4. When entering game loop, GPU has no backlog → instant 60 FPS

**Benefits:**
- **Before:** 1.7-2.3 FPS for first ~5 minutes as GPU catches up with 300+ chunk backlog
- **After:** Instant 60 FPS from first frame with no streaming backlog
- User sees "Warming up GPU..." message during ~10 second load (worth it!)
- Better UX: predictable load time vs. unpredictable multi-minute stuttering

**Why spawn radius=4 specifically:**
- Load distance = 152 blocks (renderDistance + 32)
- Load radius = 5 chunks (461 chunks in sphere)
- Spawn radius = 4 chunks (729 chunks in cube, covers entire load sphere)
- No chunks need streaming until player moves beyond initial area
- GPU warm-up covers ALL chunks that would be needed

**Combined with other GPU optimizations:**
- Buffer deletion rate limiting (10/frame max)
- Chunk unloading disabled during initial load
- Lock-free chunk iteration for minimal CPU overhead
- Spawn/load radius matching prevents streaming backlog

### Face Culling System

Intelligently determines which block faces to render based on block type and neighbors:

**Transparent Blocks (leaves, glass):**
```cpp
// Render face unless neighbor is same block type
shouldRender = (neighborBlockID != currentBlockID) && (neighborBlockID != 0);
```
- Leaves next to **same leaves** = culled (optimization)
- Leaves next to **air/dirt/other blocks** = rendered (visible)
- Prevents "invisible leaves" bug

**Opaque Blocks (dirt, stone):**
```cpp
// Render face if neighbor is not solid
shouldRender = !neighborIsSolid;
```
- Standard occlusion culling
- Hidden faces never rendered

**Liquid Blocks (water):**
```cpp
// Render face if neighbor is different liquid level or not liquid
if (neighborIsLiquid) {
    shouldRender = (currentLevel != neighborLevel);
} else {
    shouldRender = true;
}
```
- Water levels visible through water
- Water surfaces render against air/solids

**Benefits:**
- 60-90% face reduction for typical terrain
- Transparent blocks render correctly
- No z-fighting between identical transparent blocks

### Greedy Meshing

Combines adjacent identical block faces into larger quads:

**Benefits:**
- 50-80% vertex reduction
- Fewer draw calls
- Better GPU cache utilization

**Algorithm:**
1. Scan each chunk layer (XY, XZ, YZ planes)
2. Find rectangular regions of identical blocks
3. Create single quad instead of many small quads
4. Apply to all 6 cardinal directions

**Performance:**
- Mesh generation: <1ms per chunk (with pooling)
- Memory: ~14MB for full world
- FPS: 60+ stable

**UV Tiling for Merged Quads:**
Greedy meshing merges adjacent blocks into larger quads. To show individual block
textures (not stretched), UV coordinates use a special encoding:

- **Encoding:** `UV = cellIndex + (localUV × quadSize) / atlasSize`
- **Shader Decoding:**
  1. `cell = floor(UV)` - Extract atlas cell index
  2. `localUV = fract(UV) × atlasSize` - Local position scaled by quad dimensions
  3. `tiledUV = fract(localUV)` - Tile within 0-1 range
  4. `finalUV = (cell + tiledUV) / atlasSize` - Convert back to atlas space

This allows merged quads to tile the texture N times, showing block boundaries.
Exception: Animated textures (water) use the old stretched encoding.

### Texture System

**Texture Atlas:**
- All block textures packed into single 2048×2048 texture
- GPU samples from UV coordinates
- Reduces texture binding overhead

**Cube Map Textures:**
- Each block face can have different texture
- Defined in `assets/blocks/*.yaml`

```yaml
name: "grass"
textures:
  top: "grass_top.png"
  bottom: "dirt.png"
  sides: "grass_side.png"
```

**Texture Variation:**
- Random UV offsets for natural appearance
- Per-face variation via comma syntax: `"texture.png,1.5"`

## 3.3 Lighting System

### Light Types

**Sunlight:**
- Time-based intensity (0.0 = night, 1.0 = day)
- Propagates from sky downward
- Attenuates through transparent blocks

**Block Light:**
- Emitted by luminous blocks (lava, torches, etc.)
- Per-block light level (0-15)
- Propagates in all directions

### Light Calculation

**Storage:**
- `BlockLight` structure per voxel (32 KB per chunk)
- 4 bits for block light, 4 bits for sky light

**Propagation:**
1. Initialize light sources (sky, blocks)
2. Flood fill algorithm
3. Reduce intensity by 1 per block traveled
4. Stop at opaque blocks

**Smooth Lighting:**
- Interpolate light values at vertices
- Average neighboring block light levels
- Creates smooth gradients

### Performance Characteristics

**Initialization (World Load):**
- **Batched Processing**: Light propagation processes nodes in batches of 10,000
- **Progress Reporting**: Console updates show progress during initial world lighting
- **Typical Load Time**: 3-5 seconds for spawn area lighting initialization
- **Queue Size**: ~300,000-600,000 light nodes for initial propagation
- **Critical Fix Applied**: Both new AND loaded worlds initialize lighting (prevents GPU stalls)

**Runtime Updates (During Gameplay):**
- **Frame-Rate Safe**: Incremental updates prevent frame stalls
  - Max 350 light additions per frame (optimized from 500 for stable frame times)
  - Max 300 light removals per frame (higher priority)
  - Max 15 chunk mesh regenerations per frame (optimized from 10 for faster updates)
- **Sub-millisecond**: Typical update cost <1ms per frame
- **Two-Queue Algorithm**: Handles light source removal properly

**Configuration:**
- Console command: `lighting` (toggles on/off)
- Persisted to `config.ini` (enabled by default)
- Can disable lighting for performance testing
- Changes take effect immediately (chunks regenerate on movement)

**Memory Usage:**
- 32 KB per chunk (BlockLight data: 4-bit sky + 4-bit block per voxel)
- Light propagation queues: 8-160 KB depending on activity
- Dirty chunk tracking: ~20 KB for 500 chunks

### Known Limitations

**Lighting Data Persistence:**
- **RAM Cache (✅ Preserved)**: Chunks in RAM cache (500-2000 chunks) keep full lighting data
  - Instant reload when revisiting recently explored areas
  - No lighting recalculation needed for cached chunks
  - Cache provides 90%+ hit rate for typical gameplay patterns
- **Disk Persistence (❌ Not Saved)**: Lighting NOT serialized to disk in chunk files
  - Only blocks + metadata saved (`Chunk::save()` at `src/chunk.cpp:1840-1858`)
  - Lighting recalculated when loading chunks evicted from RAM cache
  - World load requires full lighting initialization (3-5 seconds for spawn area)
  - **Fixed**: Lighting now initializes during world load (prevents GPU stall bug)
- **Future Optimization**: Serialize lighting data to disk to eliminate recalculation on world load

**Historical Issues (FIXED in 2025-11-21):**

**Issue #1: Loaded Worlds Missing Lighting Init**
- ~~Loaded worlds skipped lighting initialization entirely~~
- ~~Caused 900-1100ms GPU stalls when chunks marked dirty during gameplay~~
- ~~`beginFrame()` blocked waiting for mass mesh regeneration uploads~~
- **Resolution**: Added lighting init for loaded worlds (`src/main.cpp:393-451`)

**Issue #2: Triple Mesh Generation**
- ~~New worlds generated meshes 3x: terrain → decoration → lighting~~
- ~~2/3 of mesh work was wasted (incomplete lighting)~~
- ~~Caused 6-16 second slowdown during world load~~
- **Resolution**: Defer mesh generation until after lighting completes (`src/world.cpp:207-224`)

**Issue #3: Double Mesh Generation in Loaded Worlds**
- ~~Loaded worlds meshed before lighting init (wasted work)~~
- ~~Required complete mesh regeneration after lighting~~
- ~~Added 3-8 seconds to world load time~~
- **Resolution**: Reorder operations - lighting first, then mesh (`src/main.cpp:396-451`)

**Issue #4: Excessive Console Output**
- ~~Progress reported every 10,000 nodes (30-60 flushes per world load)~~
- ~~Console I/O overhead: 100-1000ms~~
- **Resolution**: Report every 100,000 nodes instead (`src/lighting_system.cpp:76-83`)

**BFS Propagation:**
- Light spreads horizontally over multiple frames during gameplay
- Newly generated chunks may show lighting "pop-in" for 2-15 frames
- Spawn chunks have full lighting before gameplay begins
- RAM cache provides instant lighting for recently visited chunks (no pop-in on return)

## 3.4 Sky & Time System

### Time Mechanics

**Time Scale:**
- 0.0 = Midnight
- 0.25 = Sunrise
- 0.5 = Noon
- 0.75 = Sunset
- 1.0 = Midnight (wraps to 0.0)

**Time Progression:**
- Minecraft-compatible: 24000 ticks = 20 minutes
- Configurable speed: `timespeed <multiplier>`
- Can pause: `timespeed 0`

### Sky Rendering

**Dual Cube Map System:**
- **Day Sky** - Natural blue gradient with clouds
- **Night Sky** - Black with baked star texture

**Blending:**
- Smooth transition at dawn/dusk
- Blending factor based on time of day

**Sun & Moon:**
- Procedurally rendered squares (voxel aesthetic)
- Position calculated from time: `angle = timeOfDay * 2π`
- Dreamy gradient halos (orange, pink, purple)

**Stars:**
- Baked into cube map texture
- Real-time twinkling via shader
- Red, blue, and white stars

**Console Commands:**
```
skytime 0.5           # Set to noon
skytime 0.0           # Set to midnight
timespeed 10          # 10x speed (2 min cycle)
timespeed 0           # Pause time
```

## 3.5 Water System

### Water Simulation

**Cellular Automata:**
- Per-voxel water storage
- Attributes: level (0-255), flowVector (vec2), fluidType

**Simulation Rules:**
1. **Evaporation** - Water below threshold slowly disappears
2. **Gravity** - Water flows down with highest priority
3. **Horizontal Spread** - Weighted pathfinding to lowest point
4. **Flow Vectors** - Track movement direction

**Performance:**
- Updates spread across 4 frames (25% per frame)
- Hash map storage for O(1) access
- Limits chunk mesh regeneration to 5 per frame

### Water Rendering

**Visual Effects:**
- Diagonal flowing texture animation (250x speed)
- Semi-transparent blue color
- Smooth flow appearance

**Shader Implementation:**
```glsl
// Texture scrolling
vec2 scrollSpeed = vec2(0.5, -0.5) * 250.0;
vec2 scrolledUV = uv + scrollSpeed * time;
```

**Water Sources:**
- Infinite water generation at position
- Configurable flow rate
- Supports multiple fluid types (water, lava)

**Water Bodies:**
- Oceans and lakes marked as infinite
- Prevents evaporation in large bodies
- Maintains minimum water level

## 3.6 Mesh Rendering System

The engine features a complete **PBR (Physically Based Rendering) mesh pipeline** separate from the voxel rendering system, enabling rendering of arbitrary 3D models alongside voxel terrain.

### Architecture Overview

**Dual Pipeline Design:**
- **Voxel Pipeline** - Greedy meshing, chunk-based, texture atlas
- **Mesh Pipeline** - Indexed triangles, instanced rendering, PBR materials
- **Shared Resources** - Camera UBO, depth buffer, command buffer

Both pipelines render in the same frame with correct depth occlusion. Meshes and voxels can overlap and properly occlude each other.

### Core Data Structures

**MeshVertex** (44 bytes per vertex)
```cpp
struct MeshVertex {
    glm::vec3 position;    // 12 bytes - World-space position
    glm::vec3 normal;      // 12 bytes - Surface normal
    glm::vec2 texCoord;    // 8 bytes  - UV coordinates
    glm::vec3 tangent;     // 12 bytes - Tangent space (for normal mapping)
};
```

**PBRMaterial** - Metallic-Roughness Workflow
```cpp
struct PBRMaterial {
    glm::vec4 baseColor;        // Albedo color (RGB + alpha)
    float metallic;             // 0.0 = dielectric, 1.0 = metal
    float roughness;            // 0.0 = smooth, 1.0 = rough
    float emissive;             // Emissive strength
    int32_t albedoTexture;      // Texture indices (Phase 2)
    int32_t normalTexture;
    int32_t metallicRoughnessTexture;
    int32_t emissiveTexture;
};
```

**InstanceData** (80 bytes per instance)
```cpp
struct InstanceData {
    glm::mat4 transform;   // 64 bytes - Position, rotation, scale
    glm::vec4 tintColor;   // 16 bytes - Color modulation
};
```

### Mesh Loading

**OBJ Loader** - Full Wavefront OBJ Support
- Vertex positions, normals, UV coordinates
- Face definitions with triangulation
- Negative indices (relative indexing)
- Automatic normal generation if missing
- Automatic tangent calculation (Gram-Schmidt)
- Vertex deduplication (hash map keyed by position/uv/normal)

**API:**
```cpp
std::vector<PBRMaterial> materials;
std::vector<Mesh> meshes = MeshLoader::loadOBJ("assets/meshes/tree.obj", materials);
```

**Procedural Generators:**
```cpp
Mesh cube = MeshLoader::createCube(2.0f);         // Perfect cube
Mesh sphere = MeshLoader::createSphere(1.5f, 32); // UV sphere, 32 segments
Mesh cylinder = MeshLoader::createCylinder(1.0f, 3.0f, 16); // Radius, height, segments
Mesh plane = MeshLoader::createPlane(10.0f, 10.0f, 5, 5);   // Width, depth, subdivisions
```

All procedural meshes include proper UVs, normals, and tangents.

### PBR Material System

**Physically Based Rendering** - Cook-Torrance BRDF

The fragment shader implements full PBR with:
- **GGX/Trowbridge-Reitz** - Normal distribution function
- **Schlick-GGX** - Geometry function (self-shadowing)
- **Fresnel-Schlick** - Fresnel reflectance approximation

**Lighting Model:**
```glsl
// Microfacet specular BRDF
float NDF = DistributionGGX(N, H, roughness);
float G = GeometrySmith(N, V, L, roughness);
vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

// Cook-Torrance BRDF
vec3 nominator = NDF * G * F;
float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0);
vec3 specular = nominator / max(denominator, 0.001);

// Diffuse + specular
vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
vec3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;
```

**Dynamic Lighting:**
- Sun lighting rotates with time of day (synchronized with voxel world)
- Day/night ambient transitions
- Emissive materials glow independently

**Post-Processing:**
- Reinhard tone mapping: `color / (color + 1.0)`
- Gamma correction: `pow(color, 1.0 / 2.2)` for sRGB output

### MeshRenderer API

High-level interface for managing meshes, materials, and instances:

**Initialization:**
```cpp
#include "mesh/mesh_renderer.h"

MeshRenderer meshRenderer(&renderer);  // Pass VulkanRenderer
```

**Loading and Creating Meshes:**
```cpp
// Load from OBJ file
uint32_t treeMeshId = meshRenderer.loadMeshFromFile("assets/meshes/tree.obj");

// Or create procedurally
Mesh sphere = MeshLoader::createSphere(1.0f, 32);
uint32_t sphereMeshId = meshRenderer.createMesh(sphere);
```

**Material Management:**
```cpp
// Create PBR material
PBRMaterial redMaterial = PBRMaterial::createDefault();
redMaterial.baseColor = glm::vec4(1.0f, 0.2f, 0.2f, 1.0f);  // Red
redMaterial.metallic = 0.0f;   // Non-metallic
redMaterial.roughness = 0.6f;  // Slightly rough

uint32_t redMatId = meshRenderer.createMaterial(redMaterial);

// Assign material to mesh
meshRenderer.setMeshMaterial(sphereMeshId, redMatId);

// Update material properties at runtime
redMaterial.roughness = 0.3f;
meshRenderer.updateMaterial(redMatId, redMaterial);
```

**Instance Management:**
```cpp
// Create instance with transform
glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 64.0f, 5.0f));
uint32_t instanceId = meshRenderer.createInstance(sphereMeshId, transform);

// Update instance transform (e.g., for animation)
glm::mat4 newTransform = glm::rotate(transform, glm::radians(45.0f), glm::vec3(0, 1, 0));
meshRenderer.updateInstanceTransform(instanceId, newTransform);

// Update instance color tint
meshRenderer.updateInstanceColor(instanceId, glm::vec4(0.5f, 1.0f, 0.5f, 1.0f));

// Remove instance
meshRenderer.removeInstance(instanceId);
```

**Rendering:**
```cpp
// In main render loop (after voxel rendering)
world.renderWorld(cmd, ...);
meshRenderer.render(cmd);  // Renders all mesh instances
```

**Statistics:**
```cpp
size_t meshCount = meshRenderer.getMeshCount();       // Unique mesh types
size_t instanceCount = meshRenderer.getInstanceCount(); // Total instances
size_t gpuMemory = meshRenderer.getGPUMemoryUsage();   // Bytes on GPU
```

### Instance Buffer Management

**Automatic Updates:**
- Each mesh maintains a list of instance IDs
- Instance buffer rebuilt when:
  - New instance created
  - Instance transform/color updated
  - Instance removed
- Dirty flag system prevents unnecessary GPU uploads

**Implementation:**
```cpp
void MeshRenderer::updateInstanceBuffer(MeshData& meshData) {
    // Collect all instance data for this mesh
    std::vector<InstanceData> instanceArray;
    for (uint32_t instanceId : meshData.instances) {
        instanceArray.push_back(m_instances[instanceId].data);
    }

    // Upload to GPU (replaces old buffer)
    m_renderer->uploadMeshBuffers(
        instanceArray.data(), instanceArray.size(), sizeof(InstanceData), ...
    );

    meshData.instanceBufferDirty = false;
}
```

**Rendering with Instancing:**
```cpp
// Bind vertex buffer (binding 0) - mesh geometry
vkCmdBindVertexBuffers(cmd, 0, 1, &meshData.mesh.vertexBuffer, &vertexOffset);

// Bind instance buffer (binding 1) - per-instance data
vkCmdBindVertexBuffers(cmd, 1, 1, &meshData.instanceBuffer, &instanceOffset);

// Draw all instances in one call
vkCmdDrawIndexed(cmd, indexCount, instanceCount, 0, 0, 0);
```

### Vulkan Pipeline Details

**Vertex Input:**
- **Binding 0** - Per-vertex data (MeshVertex)
  - Location 0: position (vec3)
  - Location 1: normal (vec3)
  - Location 2: texCoord (vec2)
  - Location 3: tangent (vec3)
- **Binding 1** - Per-instance data (InstanceData)
  - Location 4-7: transform (mat4) - uses 4 attribute slots
  - Location 8: tintColor (vec4)

**Descriptor Sets:**
- **Set 0, Binding 0** - Camera UBO (shared with voxel pipeline)
  - Projection matrix
  - View matrix
  - Camera position
  - Time of day
- **Set 0, Binding 1** - Material UBO (per-mesh, future: per-draw with descriptor indexing)
  - PBR material properties

**Pipeline State:**
- Depth testing: Enabled (VK_COMPARE_OP_LESS)
- Depth write: Enabled
- Culling: Back-face (VK_CULL_MODE_BACK_BIT)
- Front face: Counter-clockwise
- Blending: Disabled (opaque meshes only in Phase 1)

### Performance Characteristics

**Memory Usage:**
- Mesh vertex: 44 bytes
- Index: 4 bytes (uint32)
- Material UBO: 64 bytes (GPU-aligned)
- Instance data: 80 bytes

**Example Mesh:**
- 1000-triangle mesh = 44 KB vertices + 12 KB indices = ~56 KB

**Rendering Performance:**
- **Unique meshes**: 500+ @ 60 FPS
- **Instanced meshes**: 5,000+ @ 60 FPS (100+ instances per mesh type)
- **Draw calls**: <50 per frame with instancing

**GPU Memory Budget:**
- 500 unique meshes × 56 KB = ~28 MB
- 5,000 instances × 80 bytes = ~400 KB
- Materials: negligible (~32 KB for 500 materials)
- Total: <30 MB for typical world decoration

### Use Cases

**World Decoration:**
- Trees, rocks, bushes (instanced)
- Buildings, structures (unique or instanced)
- Decorative props (furniture, tools)

**Future Enhancements:**
- NPCs, creatures, vehicles (animated meshes)
- Items, weapons (handheld meshes)
- Particles, effects (billboarded meshes)

### Integration Example

```cpp
// main.cpp - After world initialization

// Create mesh renderer
MeshRenderer meshRenderer(&renderer);

// Load tree mesh
uint32_t treeMeshId = meshRenderer.loadMeshFromFile("assets/meshes/oak_tree.obj");

// Create wood material
PBRMaterial woodMat = PBRMaterial::createDefault();
woodMat.baseColor = glm::vec4(0.4f, 0.3f, 0.2f, 1.0f);  // Brown
woodMat.roughness = 0.8f;
woodMat.metallic = 0.0f;
uint32_t woodMatId = meshRenderer.createMaterial(woodMat);
meshRenderer.setMeshMaterial(treeMeshId, woodMatId);

// Spawn trees in world
for (int i = 0; i < 100; i++) {
    glm::vec3 pos = getRandomTreePosition();
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), pos);
    transform = glm::rotate(transform, randomAngle(), glm::vec3(0, 1, 0));
    meshRenderer.createInstance(treeMeshId, transform);
}

// In render loop
while (running) {
    renderer.beginFrame();
    world.renderWorld(cmd);      // Voxel terrain
    meshRenderer.render(cmd);    // Mesh objects (trees, etc.)
    renderer.endFrame();
}
```

### Known Limitations (Phase 1)

- **No Textures** - Material colors only (shaders support textures, loading not implemented)
- **No glTF** - OBJ only (glTF 2.0 planned for Phase 2)
- **No Animations** - Static meshes only (skeletal animation planned for Phase 6)
- **No Transparency** - Opaque meshes only (alpha blending planned for Phase 2)
- **No Shadows** - Phase 5 feature
- **No LOD** - Level-of-detail system planned for Phase 4

### Future Roadmap

**Phase 2: Textures & Transparency**
- Texture loading system (PNG, DDS)
- Descriptor indexing for texture arrays
- glTF 2.0 loader
- Alpha blending for transparent meshes
- Normal mapping (shader ready)

**Phase 3: Advanced Materials**
- Metallic-roughness texture maps
- Emissive texture maps
- Occlusion maps
- Material texture caching

**Phase 4: Performance Optimization**
- Mega-buffer migration (like voxel indirect drawing)
- GPU frustum culling (compute shader)
- LOD system (multiple mesh detail levels)
- Occlusion culling

**Phase 5: Lighting & Shadows**
- Shadow mapping integration
- Point light shadows
- Contact shadows

**Phase 6: Animation**
- Skeletal animation system
- Keyframe interpolation
- Animation blending
- IK (Inverse Kinematics)

## 3.7 Save/Load System

### File Format

**World Metadata:** `worlds/<worldname>/world.dat`
```cpp
Version: 1 (uint32_t)
WorldName: string
Seed: uint32_t
PlayerPosition: vec3
PlayerRotation: vec3
```

**Chunks:** `worlds/<worldname>/chunks/<x>_<y>_<z>.chunk`
```cpp
Version: 2 (uint32_t)
ChunkX, ChunkY, ChunkZ: int32_t
Compressed block data (RLE)
Compressed metadata (RLE)
Compressed lighting data (RLE)
```

**Compression:**
- RLE (Run-Length Encoding) for chunks
- Typical compression: 192 KB → 3-9 KB (80-95% reduction)
- Empty chunks not saved (culled)

### Auto-Save System

**Features:**
- Periodic saves every N minutes
- Dirty chunk tracking
- Asynchronous save operations
- Progress indication

**World Browser:**
- List all saved worlds
- Load/delete world UI
- World metadata display

### Implementation

**Save World:**
```cpp
world->saveWorld();  // Save all modified chunks
player->savePlayerState();
inventory->save();
```

**Load World:**
```cpp
world->loadWorld(worldName);
player->loadPlayerState();
inventory->load();
```

---

# 4. Architecture & Design

## 4.1 Threading Model

### Main Thread (Rendering)

**Responsibilities:**
- Render loop (60 FPS target)
- Input handling
- UI rendering (ImGui)
- GPU command submission

**Frame Structure:**
```cpp
while (running) {
    processInput();
    updatePlayerPos();
    processCompletedChunks();  // Non-blocking
    renderFrame();
    presentFrame();
}
```

### Worker Threads (Background)

**Chunk Generation:**
- N worker threads (typically 4-8)
- Process priority queue of chunk requests
- Three-tier loading:
  1. RAM cache (instant)
  2. Disk load (compressed)
  3. Fresh generation (procedural)

**Thread Safety:**
- `std::shared_mutex` for world chunk map
- Priority queue with condition variables
- Lock-free completed chunk queue

### Synchronization

**Critical Sections:**
```cpp
// World chunk map access
std::shared_lock<std::shared_mutex> readLock(m_chunkMapMutex);
// Read operations

std::unique_lock<std::shared_mutex> writeLock(m_chunkMapMutex);
// Write operations
```

**Lock Hierarchy:**
1. World mutex (outermost)
2. Chunk mutex (inner)
3. Never lock in reverse order (prevents deadlock)

## 4.2 Memory Management

### Memory Breakdown

**Per Chunk:**
- Block data: 192 KB (blocks + metadata + lighting)
- Mesh buffers: ~260 KB (vertices + indices)
- Total: ~452 KB per chunk

**Full World (12×512×12 chunks = 73,728 chunks):**
- Total: ~33.4 GB

**Streamed World (13-chunk radius = 2,197 chunks):**
- Total: ~2.1 GB

### Memory Optimization Strategies

**1. Chunk Pooling**
```cpp
class ChunkPool {
    std::vector<std::unique_ptr<Chunk>> m_allocated;
    std::queue<Chunk*> m_available;

    Chunk* acquire(int x, int y, int z);
    void release(Chunk* chunk);
};
```

**Benefits:**
- Reduces allocation overhead 20-30%
- Prevents heap fragmentation
- Cache-friendly memory reuse

**2. Mesh Buffer Pooling**
```cpp
class MeshBufferPool {
    std::vector<std::vector<Vertex>> m_vertexPools;
    std::vector<std::vector<uint32_t>> m_indexPools;

    std::vector<Vertex>& acquireVertexBuffer();
    void releaseVertexBuffer(std::vector<Vertex>& buffer);
};
```

**Benefits:**
- Saves 40-60% mesh generation time
- Eliminates repeated allocations
- Pre-reserved capacity (50,000 vertices)

**3. RAM Cache for Unloaded Chunks**
```cpp
class ChunkCache {
    std::unordered_map<ChunkPos, std::unique_ptr<Chunk>> m_cache;
    std::deque<ChunkPos> m_lruQueue;  // Least recently used
    size_t m_maxCacheSize = 1000;     // ~450 MB of cached chunks

    Chunk* get(const ChunkPos& pos) {
        auto it = m_cache.find(pos);
        if (it != m_cache.end()) {
            updateLRU(pos);  // Mark as recently used
            return it->second.get();
        }
        return nullptr;
    }

    void add(ChunkPos pos, std::unique_ptr<Chunk> chunk) {
        if (m_cache.size() >= m_maxCacheSize) {
            evictLRU();  // Free space by writing to disk
        }
        m_cache[pos] = std::move(chunk);
        m_lruQueue.push_back(pos);
    }

    void evictLRU() {
        auto oldest = m_lruQueue.front();
        m_lruQueue.pop_front();
        m_cache[oldest]->saveToDisk();  // Only now write to disk
        m_cache.erase(oldest);
    }
};

void Chunk::unload() {
    // Move to RAM cache (fast - keeps block data)
    world->getChunkCache()->add(m_position, this);
    // Free GPU buffers only
    freeGPUBuffers();
    m_isLoaded = false;
}
```

**Priority:**
- Unload chunks beyond render distance to RAM cache (instant)
- Keep 13-chunk radius active in memory
- Evict from cache to disk only when:
  - Cache exceeds size limit (LRU eviction)
  - Explicit save command / auto-save triggered
  - Application shutdown

**Benefits:**
- Near-instant re-loading of recently visited chunks
- Reduces disk I/O by 90%+
- Smooth experience when moving back and forth

### Memory Budget

| RAM   | Safe Budget | Active Chunks | Cache Chunks | Total Chunks | Radius |
|-------|-------------|---------------|--------------|--------------|--------|
| 4 GB  | 2 GB        | 2,197 (r=13)  | 500          | 2,697        | 10-13  |
| 8 GB  | 5 GB        | 6,859 (r=17)  | 1,000        | 7,859        | 13-17  |
| 16 GB | 10 GB       | 11,449 (r=20) | 2,000        | 13,449       | 17-20  |
| 32 GB | 20 GB       | 24,389 (r=26) | 5,000        | 29,389       | 21-26  |

**Memory Allocation:**
- Active chunks: Fully loaded (blocks + mesh + GPU buffers)
- Cached chunks: Blocks only (~192 KB each), no mesh/GPU
- Cache provides instant re-load for recently visited areas

## 4.3 Chunk Lifecycle

### State Machine

```
INACTIVE → QUEUED → GENERATING → ACTIVE → RENDER → (DIRTY) → CACHED → UNLOADED
                        ↑______________|                              |
                                (cache hit - instant reload)←─────────┘
```

**States:**
- **INACTIVE** - Chunk object exists but not loaded
- **QUEUED** - Requested for generation
- **GENERATING** - Worker thread processing (check cache → disk → generate)
- **ACTIVE** - Block data loaded in RAM
- **RENDER** - GPU buffers created and rendering
- **DIRTY** - Modified, needs mesh regeneration
- **CACHED** - Block data in RAM cache, GPU buffers freed (instant reload)
- **UNLOADED** - Evicted from cache to disk, RAM freed

### Lifecycle Operations

**Load Chunk:**
1. Check RAM cache
2. If not cached, check disk
3. If not on disk, generate procedurally
4. Decorate (trees, structures)
5. Calculate lighting
6. Generate mesh
7. Upload to GPU

**Update Chunk:**
1. Modify block data
2. Mark as dirty
3. Recalculate lighting
4. Regenerate mesh
5. Re-upload to GPU

**Unload Chunk:**
1. Move to RAM cache (keep block data)
2. Free GPU buffers only
3. Mark as inactive (but cached)

**Evict from Cache to Disk:**
1. Check if cache is full
2. Select least recently used (LRU) chunk
3. Serialize to disk (if modified)
4. Free block data from cache
5. Remove from cache map

## 4.4 Priority System

### Chunk Loading Priority

**Priority Calculation:**
```cpp
priority = -distance_squared(playerPos, chunkPos)
```

Higher priority (closer chunks) loaded first.

**Priority Queue:**
- Implemented with `std::priority_queue`
- Worker threads pop highest priority
- Dynamically updated as player moves

### Load Radius Management

**Zones:**
- **Active Zone** (0-13 chunks) - Fully loaded with GPU buffers and simulated
- **Render Zone** (13-20 chunks) - Rendered but not simulated
- **Cache Zone** (recently visited) - Block data in RAM, GPU freed (LRU cache)
- **Disk Zone** (>20 chunks + old cache) - Evicted to disk and unloaded

**Dynamic Adjustment:**
- Increase radius with more RAM
- Decrease radius for performance

---

# 5. Development Guide

## 5.1 Project Structure

```
voxel-engine/
├── src/                      # C++ source files
│   ├── main.cpp              # Entry point
│   ├── vulkan_renderer.cpp   # Vulkan rendering
│   ├── chunk.cpp             # Chunk generation/meshing
│   ├── world.cpp             # World management
│   ├── player.cpp            # Player controller
│   ├── console.cpp           # Developer console
│   ├── water_simulation.cpp  # Water system
│   ├── biome_system.cpp      # Biome generation
│   ├── mesh/                 # Mesh rendering system
│   │   ├── mesh.cpp          # Mesh data structures
│   │   ├── mesh_loader.cpp   # OBJ loading + procedural generation
│   │   └── mesh_renderer.cpp # High-level mesh API
│   └── ...
├── include/                  # Header files
│   ├── vulkan_renderer.h
│   ├── chunk.h
│   ├── world.h
│   ├── mesh/                 # Mesh rendering headers
│   │   ├── mesh.h            # Mesh data structures
│   │   ├── mesh_loader.h     # Loading API
│   │   └── mesh_renderer.h   # Rendering API
│   └── ...
├── shaders/                  # GLSL shaders
│   ├── shader.vert           # Voxel vertex shader
│   ├── shader.frag           # Voxel fragment shader
│   ├── mesh.vert             # Mesh vertex shader (PBR)
│   ├── mesh.frag             # Mesh fragment shader (PBR)
│   ├── skybox.vert           # Skybox vertex shader
│   ├── skybox.frag           # Skybox fragment shader
│   ├── line.vert             # Line rendering
│   ├── line.frag
│   └── compile.bat           # Shader compilation script
├── assets/                   # Game assets
│   ├── blocks/               # Block definitions (YAML)
│   ├── biomes/               # Biome definitions (YAML)
│   ├── structures/           # Structure definitions (YAML)
│   ├── meshes/               # 3D mesh files (OBJ)

├── docs/                     # Documentation
│   ├── BUILD_INSTRUCTIONS.MD # Build Guides
│   └── ENGINE_HANDBOOK.MD    # Systems guides
├── external/                 # Third-party libraries
│   ├── imgui-1.91.9b/
│   ├── glfw-3.4/
│   ├── yaml-cpp/
│   └── ...
├── build/                    # Build output
├── CMakeLists.txt            # CMake configuration
└── config.ini                # Runtime configuration

```

## 5.2 Building from Source

### CMake Configuration

**CMakeLists.txt** handles:
- Compiler settings (C++17)
- Vulkan SDK detection
- Library linking (GLFW, yaml-cpp, ImGui)
- Shader compilation
- Asset copying

### Debug vs Release

**Debug Build:**
```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
```
- Vulkan validation layers enabled
- Debug symbols included
- ~3-5x slower due to checks

**Release Build:**
```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
```
- Optimizations enabled
- No validation layers
- Best performance

### Custom Build Options

```cmake
# Enable specific features
option(ENABLE_TESTING "Build test suite" OFF)
option(ENABLE_PROFILING "Enable profiling markers" OFF)
```

## 5.3 Adding New Features

### Adding a New Block Type

1. **Create texture** - Place PNG in `assets/textures/`

2. **Define block** - Create YAML in `assets/blocks/`:
```yaml
name: "my_block"
textures:
  all: "my_block.png"
properties:
  solid: true
  transparent: false
  light_level: 0
```

3. **Register block** - Block system auto-loads YAML files

4. **Use in code:**
```cpp
int blockID = blockRegistry.getBlockID("my_block");
world.setBlock(pos, blockID);
```

### Adding a Console Command

1. **Define command function** in `src/console_commands.cpp`:
```cpp
void cmd_mycommand(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        console.addLine("Usage: mycommand <param>");
        return;
    }
    // Command implementation
    console.addLine("Command executed!");
}
```

2. **Register command** in `Console::init()`:
```cpp
registerCommand("mycommand", cmd_mycommand,
    "Description of my command");
```

3. **Add autocomplete** (optional):
```cpp
console.addSuggestion("mycommand");
console.addSuggestion("mycommand value1");
console.addSuggestion("mycommand value2");
```

### Adding a New Biome

1. **Create biome file** in `assets/biomes/my_biome.yaml`:
```yaml
name: "My Biome"
temperature: 60
moisture: 50
age: 45
activity: 60
surface_block: "grass"
sub_surface_block: "dirt"
stone_block: "stone"
```

2. **Biome loads automatically** on startup

3. **Test in-game:**
```
# Set world seed for testing
# Biome will generate in appropriate temperature/moisture
```

## 5.4 Shader Development

### Shader Files

**Vertex Shader** (`shaders/shader.vert`):
```glsl
#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    float time;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;

void main() {
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragTexCoord = inTexCoord;
}
```

**Fragment Shader** (`shaders/shader.frag`):
```glsl
#version 450

layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(texSampler, fragTexCoord) * vec4(fragColor, 1.0);
}
```

### Compiling Shaders

**Windows:**
```cmd
cd shaders
"%VULKAN_SDK%\Bin\glslc.exe" shader.vert -o vert.spv
"%VULKAN_SDK%\Bin\glslc.exe" shader.frag -o frag.spv
```

**Linux:**
```bash
cd shaders
glslc shader.vert -o vert.spv
glslc shader.frag -o frag.spv
```

**Automated:**
```bash
# Windows
cd shaders
compile.bat

# Linux
cd shaders
./compile.sh
```

### Testing Shaders

1. Modify shader source
2. Recompile to SPIR-V
3. Restart game (or implement hot reload)
4. Check for validation layer errors

## 5.5 Testing

### Test Suite

**Location:** `docs/testing/`

**Test Categories:**
- Correctness tests (6 tests)
- Memory tests (6 tests)
- Performance tests (7 tests)
- Stress tests (8 tests)

**Running Tests:**
```bash
cd build
ctest --output-on-failure
```

### Performance Gates

| Metric | Threshold |
|--------|-----------|
| Chunk generation | < 5ms |
| Mesh generation | < 3ms |
| Block access | < 10 µs |
| World loading | < 20ms/chunk |
| Frame time | < 33ms (30 FPS minimum) |

### Memory Leak Testing

**Valgrind (Linux):**
```bash
valgrind --leak-check=full --show-leak-kinds=all ./voxel-engine
```

**Visual Studio (Windows):**
- Enable CRT debug heap
- Check for memory leaks on exit

---

# 6. API Reference

## 6.1 Core Classes

### World

**Header:** `include/world.h`

**Key Methods:**
```cpp
class World {
public:
    // Initialization
    void generateWorld();
    void loadWorld(const std::string& worldName);
    void saveWorld();

    // Block access
    int getBlockAt(int x, int y, int z);
    void setBlockAt(int x, int y, int z, int blockID);

    // Chunk management
    Chunk* getChunkAt(int chunkX, int chunkY, int chunkZ);
    void unloadChunk(int chunkX, int chunkY, int chunkZ);

    // Streaming
    void updatePlayerPosition(const glm::vec3& playerPos);
    void processChunkQueue();

private:
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>> m_chunkMap;
    std::shared_mutex m_chunkMapMutex;
    std::priority_queue<ChunkRequest> m_loadQueue;
};
```

### Chunk

**Header:** `include/chunk.h`

**Key Methods:**
```cpp
class Chunk {
public:
    // Generation
    void generate(const BiomeMap* biomeMap);
    void generateMesh(World* world, bool callerHoldsLock = false);

    // Block access
    int getBlock(int x, int y, int z) const;
    void setBlock(int x, int y, int z, int blockID);

    // State management
    bool isLoaded() const;
    bool isDirty() const;
    void markDirty();

    // Persistence
    void saveToDisk(const std::string& worldPath);
    bool loadFromDisk(const std::string& worldPath);

private:
    int m_blocks[32][32][32];                    // 128 KB
    uint8_t m_blockMetadata[32][32][32];         // 32 KB
    std::vector<BlockLight> m_lightData;         // 32 KB
    std::vector<Vertex> m_vertices;
    std::vector<uint32_t> m_indices;
};
```

### Player

**Header:** `include/player.h`

**Key Methods:**
```cpp
class Player {
public:
    // Movement
    void update(float deltaTime);
    void processInput(GLFWwindow* window, float deltaTime);

    // Position
    glm::vec3 getPosition() const;
    void setPosition(const glm::vec3& pos);
    void teleport(float x, float y, float z);

    // Physics
    void setNoclip(bool enabled);
    bool isNoclip() const;

    // Interaction
    BlockTarget getTargetedBlock(World* world);
    void breakBlock(World* world);
    void placeBlock(World* world, int blockID);

    // Persistence
    void savePlayerState(const std::string& worldPath);
    void loadPlayerState(const std::string& worldPath);
};
```

### VulkanRenderer

**Header:** `include/vulkan_renderer.h`

**Key Methods:**
```cpp
class VulkanRenderer {
public:
    // Initialization
    void init(GLFWwindow* window);
    void cleanup();

    // Frame rendering
    void beginFrame();
    void renderChunk(Chunk* chunk);
    void renderSkybox();
    void endFrame();

    // Buffer management
    void createVertexBuffer(const std::vector<Vertex>& vertices,
                           VkBuffer& buffer, VkDeviceMemory& memory);
    void createIndexBuffer(const std::vector<uint32_t>& indices,
                          VkBuffer& buffer, VkDeviceMemory& memory);

    // Texture management
    void loadTexture(const std::string& path, VkImage& image,
                    VkDeviceMemory& memory);
    void createTextureAtlas();

private:
    VkInstance m_instance;
    VkDevice m_device;
    VkSwapchainKHR m_swapchain;
    VkPipeline m_graphicsPipeline;
    VkCommandBuffer m_commandBuffer;
};
```

### Console

**Header:** `include/console.h`

**Key Methods:**
```cpp
class Console {
public:
    // Commands
    void registerCommand(const std::string& name,
                        CommandFunc func,
                        const std::string& help);
    void executeCommand(const std::string& input);

    // Output
    void addLine(const std::string& text);
    void clear();

    // ConVars
    void registerConVar(const std::string& name,
                       float defaultValue,
                       bool archive = false);
    float getConVar(const std::string& name);
    void setConVar(const std::string& name, float value);

    // UI
    void render();
    void toggle();
    bool isOpen() const;
};
```

### MeshRenderer

**Header:** `include/mesh/mesh_renderer.h`

**Key Methods:**
```cpp
class MeshRenderer {
public:
    // Constructor
    explicit MeshRenderer(VulkanRenderer* renderer);
    ~MeshRenderer();

    // Mesh management
    uint32_t loadMeshFromFile(const std::string& filepath);
    uint32_t createMesh(const Mesh& mesh);
    void removeMesh(uint32_t meshId);

    // Material management
    uint32_t createMaterial(const PBRMaterial& material);
    void updateMaterial(uint32_t materialId, const PBRMaterial& material);
    void setMeshMaterial(uint32_t meshId, uint32_t materialId);

    // Instance management
    uint32_t createInstance(uint32_t meshId, const glm::mat4& transform,
                           const glm::vec4& tintColor = glm::vec4(1.0f));
    void updateInstanceTransform(uint32_t instanceId, const glm::mat4& transform);
    void updateInstanceColor(uint32_t instanceId, const glm::vec4& tintColor);
    void removeInstance(uint32_t instanceId);

    // Rendering
    void render(VkCommandBuffer cmd);

    // Statistics
    size_t getMeshCount() const;
    size_t getInstanceCount() const;
    size_t getGPUMemoryUsage() const;

private:
    VulkanRenderer* m_renderer;
    std::unordered_map<uint32_t, MeshData> m_meshes;
    std::unordered_map<uint32_t, MaterialData> m_materials;
    std::unordered_map<uint32_t, InstanceInfo> m_instances;
};
```

### MeshLoader

**Header:** `include/mesh/mesh_loader.h`

**Key Methods:**
```cpp
class MeshLoader {
public:
    // OBJ file loading
    static std::vector<Mesh> loadOBJ(const std::string& filepath,
                                     std::vector<PBRMaterial>& materials);

    // Procedural generation
    static Mesh createCube(float size);
    static Mesh createSphere(float radius, int segments);
    static Mesh createCylinder(float radius, float height, int segments);
    static Mesh createPlane(float width, float depth, int subdivisionsX, int subdivisionsZ);
};
```

### Mesh

**Header:** `include/mesh/mesh.h`

**Key Methods:**
```cpp
struct Mesh {
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
    uint32_t materialIndex;

    // GPU buffers
    VkBuffer vertexBuffer;
    VkBuffer indexBuffer;
    VkDeviceMemory vertexMemory;
    VkDeviceMemory indexMemory;

    // Bounding box
    glm::vec3 boundingBoxMin;
    glm::vec3 boundingBoxMax;

    // Methods
    void calculateNormals();
    void calculateTangents();
    void calculateBoundingBox();
    bool hasGPUBuffers() const;
    size_t getGPUMemoryUsage() const;
};
```

## 6.2 Utility Functions

### Block Operations

```cpp
// Get block at world position
int getBlockAt(World* world, int x, int y, int z);

// Set block at world position
void setBlockAt(World* world, int x, int y, int z, int blockID);

// Convert world to chunk coordinates
ChunkCoord worldToChunkCoord(int x, int y, int z);

// Convert chunk to world coordinates
glm::ivec3 chunkToWorldCoord(const ChunkCoord& coord);
```

### Math Utilities

```cpp
// Distance calculation
float distance3D(const glm::vec3& a, const glm::vec3& b);

// Ray-box intersection
bool rayBoxIntersection(const glm::vec3& rayOrigin,
                       const glm::vec3& rayDir,
                       const glm::vec3& boxMin,
                       const glm::vec3& boxMax,
                       float& tMin);

// Clamp value
template<typename T>
T clamp(T value, T min, T max);
```

## 6.3 Event System

The voxel engine features a comprehensive event system inspired by Minecraft Forge, providing flexible and extensible game event handling with priority-based listeners, cancellation support, and thread-safe dispatch.

### Overview

The event system consists of three main components:

1. **Event Types** (`event_types.h`) - Defines all event classes and enums
2. **EventDispatcher** (`event_dispatcher.h`) - Thread-safe event queue and distribution
3. **Event Handlers** - User-defined callback functions that respond to events

**Key Features:**
- Thread-safe asynchronous and synchronous event dispatch
- Priority-based listener ordering (LOWEST to HIGHEST + MONITOR)
- Event cancellation to prevent default behavior
- Filtered listeners for conditional event processing
- Separate main-thread queue for GPU operations
- Monitor-only listeners for logging without side effects

### Event Types

All events inherit from the base `Event` class and include a timestamp and cancellation state.

#### Block Events

**BLOCK_BREAK** - Fires when a block is broken (cancellable)
```cpp
struct BlockBreakEvent : Event {
    glm::ivec3 position;      // World position of the block
    int blockID;              // ID of the block being broken
    BreakCause cause;         // PLAYER, EXPLOSION, WATER, GRAVITY, SCRIPT, UNKNOWN
    int breakerEntityID;      // Entity ID of breaker (-1 if not an entity)
};
```
**Use cases:** Prevent breaking protected blocks, drop custom items, trigger effects

**BLOCK_PLACE** - Fires when a block is placed (cancellable)
```cpp
struct BlockPlaceEvent : Event {
    glm::ivec3 position;      // Position where block will be placed
    int blockID;              // ID of the block being placed
    int placerEntityID;       // Entity ID of the placer
    glm::ivec3 placedAgainst; // Position of block placed against
};
```
**Use cases:** Prevent placement in protected areas, validate placement rules, trigger effects

**BLOCK_INTERACT** - Fires when a player right-clicks a block (cancellable)
```cpp
struct BlockInteractEvent : Event {
    glm::ivec3 position;      // Position of the interacted block
    int blockID;              // ID of the block
    int entityID;             // Entity ID performing interaction
    bool isRightClick;        // True for right-click, false for left-click
    int heldItemID;           // ID of held item (-1 if empty hand)
};
```
**Use cases:** Open GUIs (chests, furnaces), trigger actions (buttons, levers), handle tool interactions

**BLOCK_STEP** - Fires when an entity steps on a block
```cpp
struct BlockStepEvent : Event {
    glm::ivec3 position;      // Position of the block being stepped on
    int blockID;              // ID of the block
    int entityID;             // Entity ID stepping on the block
};
```
**Use cases:** Pressure plates, farmland trampling, speed/jump boosts, damage floors

**BLOCK_UPDATE** - Fires when a block receives a scheduled update tick
```cpp
struct BlockUpdateEvent : Event {
    glm::ivec3 position;      // Position of the block
    int blockID;              // ID of the block
};
```
**Use cases:** Crop growth, liquid flow, redstone updates, random ticks

#### Neighbor Events

**NEIGHBOR_CHANGED** - Fires when an adjacent block changes
```cpp
struct NeighborChangedEvent : Event {
    glm::ivec3 position;       // Position of block receiving notification
    glm::ivec3 neighborPos;    // Position of changed neighbor
    int oldBlockID;            // Previous block ID at neighbor position
    int newBlockID;            // New block ID at neighbor position
};
```
**Use cases:** Redstone wire updates, torch physics, water/lava flow, grass spreading

#### Chunk Events

**CHUNK_LOAD** - Fires when a chunk is loaded
```cpp
struct ChunkLoadEvent : Event {
    int chunkX, chunkY, chunkZ;  // Chunk coordinates
    bool isNewChunk;             // True if newly generated, false if loaded from disk
};
```
**Use cases:** Initialize chunk data, populate with entities, schedule block updates

**CHUNK_UNLOAD** - Fires before a chunk is unloaded
```cpp
struct ChunkUnloadEvent : Event {
    int chunkX, chunkY, chunkZ;  // Chunk coordinates
};
```
**Use cases:** Save custom chunk data, clean up resources, remove entities

#### Player Events

**PLAYER_MOVE** - Fires when a player moves (cancellable)
```cpp
struct PlayerMoveEvent : Event {
    glm::vec3 oldPosition;    // Previous position
    glm::vec3 newPosition;    // New position (can be modified)
    int playerID;             // Player entity ID (0 for local player)
};
```
**Use cases:** Region protection, movement restrictions, teleport triggers, anti-cheat

**PLAYER_JUMP** - Fires when a player jumps
```cpp
struct PlayerJumpEvent : Event {
    glm::vec3 position;       // Position where jump occurred
    int playerID;             // Player entity ID
};
```
**Use cases:** Modify jump height, prevent jumping, custom effects, statistics

**PLAYER_LAND** - Fires when a player lands after falling
```cpp
struct PlayerLandEvent : Event {
    glm::vec3 position;       // Landing position
    float fallDistance;       // Distance fallen in blocks
    int playerID;             // Player entity ID
};
```
**Use cases:** Calculate fall damage, landing effects, ground slam abilities

#### Time Events

**TIME_CHANGE** - Fires when world time changes
```cpp
struct TimeChangeEvent : Event {
    float oldTime;  // Previous time (0.0 = midnight, 0.5 = noon)
    float newTime;  // New time (0.0 = midnight, 0.5 = noon)
};
```
**Use cases:** Update time-dependent systems, trigger time-based events

**DAY_START** - Fires at sunrise
**NIGHT_START** - Fires at sunset

#### Custom Events

**CUSTOM** - Flexible event for mods and scripts
```cpp
struct CustomEvent : Event {
    std::string eventName;    // Event identifier
    std::any data;            // Custom data payload (any type)
};
```
**Use cases:** Mod-specific events, quest triggers, custom game modes

### Event Priorities

Listeners are executed in priority order (higher priority = called first):

```cpp
enum class EventPriority {
    LOWEST = 0,   // Called last
    LOW = 1,      // Low priority
    NORMAL = 2,   // Default priority
    HIGH = 3,     // High priority
    HIGHEST = 4,  // Called first
    MONITOR = 5   // For logging/monitoring only, cannot cancel, always called
};
```

**Priority Guidelines:**
- **HIGHEST** - Permission checks, protection systems
- **HIGH** - Core game mechanics that should run early
- **NORMAL** - Standard gameplay features (default)
- **LOW** - Optional features, quality-of-life additions
- **LOWEST** - Cleanup, final processing
- **MONITOR** - Logging, statistics (read-only, no cancellation)

### Using the Event System

#### Subscribing to Events

```cpp
#include "event_dispatcher.h"
#include "event_types.h"

// Get the singleton dispatcher
auto& dispatcher = EventDispatcher::instance();

// Subscribe to an event
ListenerHandle handle = dispatcher.subscribe(
    EventType::BLOCK_BREAK,
    [](Event& e) {
        auto& breakEvent = static_cast<BlockBreakEvent&>(e);

        // Check if it's bedrock
        if (breakEvent.blockID == 1) {  // Bedrock ID
            breakEvent.cancel();  // Prevent breaking
            // Log or notify player
        }
    },
    EventPriority::HIGHEST,  // Check permissions first
    "bedrock_protection"     // Owner identifier
);
```

#### Filtered Event Listeners

Only receive events that match a filter condition:

```cpp
// Only listen to block breaks caused by players
dispatcher.subscribeFiltered(
    EventType::BLOCK_BREAK,
    [](Event& e) {
        auto& breakEvent = static_cast<BlockBreakEvent&>(e);
        // Handle player-caused breaks
        logPlayerAction(breakEvent.breakerEntityID, breakEvent.position);
    },
    [](const Event& e) {
        auto& breakEvent = static_cast<const BlockBreakEvent&>(e);
        return breakEvent.cause == BreakCause::PLAYER;
    },
    EventPriority::MONITOR,  // Just logging, don't interfere
    "player_logger"
);
```

#### Dispatching Events

**Asynchronous (queued)** - Event processed on handler thread:
```cpp
auto event = std::make_unique<BlockBreakEvent>(
    glm::ivec3(10, 20, 30),  // position
    5,                       // blockID
    BreakCause::PLAYER,      // cause
    0                        // breakerEntityID
);
dispatcher.dispatch(std::move(event));
```

**Synchronous (immediate)** - Event processed on calling thread:
```cpp
BlockBreakEvent event(glm::ivec3(10, 20, 30), 5, BreakCause::PLAYER, 0);
dispatcher.dispatchImmediate(event);

// Check if event was cancelled
if (!event.isCancelled()) {
    // Proceed with breaking the block
}
```

**Main Thread Queue** - For GPU operations:
```cpp
// Queue event for processing on main thread
auto event = std::make_unique<ChunkLoadEvent>(0, 0, 0, true);
dispatcher.queueForMainThread(std::move(event));

// In main game loop:
dispatcher.processMainThreadQueue();
```

#### Unsubscribing

```cpp
// Unsubscribe specific listener
dispatcher.unsubscribe(handle);

// Unsubscribe all listeners for an owner
dispatcher.unsubscribeAll("bedrock_protection");

// Unsubscribe all listeners for an event type
dispatcher.unsubscribeAll(EventType::BLOCK_BREAK);
```

#### Starting/Stopping the Dispatcher

```cpp
// Start event handler thread (call during engine initialization)
dispatcher.start();

// Check if running
if (dispatcher.isRunning()) {
    // Dispatch events...
}

// Stop event handler thread (call during engine shutdown)
dispatcher.stop();  // Processes remaining queued events before stopping
```

#### Statistics

```cpp
// Get current queue size
size_t queueSize = dispatcher.getQueueSize();

// Get listener counts
size_t blockBreakListeners = dispatcher.getListenerCount(EventType::BLOCK_BREAK);
size_t totalListeners = dispatcher.getTotalListenerCount();

// Get processing statistics
uint64_t eventsProcessed = dispatcher.getEventsProcessed();
uint64_t eventsCancelled = dispatcher.getEventsCancelled();
```

### Example: Custom Block Behavior

Create a pressure plate that triggers when stepped on:

```cpp
// Subscribe to step events
dispatcher.subscribe(
    EventType::BLOCK_STEP,
    [](Event& e) {
        auto& stepEvent = static_cast<BlockStepEvent&>(e);

        // Check if it's a pressure plate block (ID 42)
        if (stepEvent.blockID == 42) {
            // Activate redstone signal
            activateRedstone(stepEvent.position);

            // Play sound effect
            playSoundAt(stepEvent.position, "click.wav");

            // Spawn particles
            spawnParticles(stepEvent.position, ParticleType::REDSTONE);
        }
    },
    EventPriority::NORMAL,
    "pressure_plate_handler"
);
```

### Example: Protected Region System

Prevent breaking/placing blocks in protected areas:

```cpp
struct ProtectedRegion {
    glm::ivec3 min;
    glm::ivec3 max;
};
std::vector<ProtectedRegion> protectedRegions;

// Prevent block breaking in protected regions
dispatcher.subscribe(EventType::BLOCK_BREAK, [](Event& e) {
    auto& breakEvent = static_cast<BlockBreakEvent&>(e);
    for (const auto& region : protectedRegions) {
        if (isInRegion(breakEvent.position, region)) {
            breakEvent.cancel();
            notifyPlayer(breakEvent.breakerEntityID, "This area is protected!");
            break;
        }
    }
}, EventPriority::HIGHEST, "region_protection");

// Prevent block placement in protected regions
dispatcher.subscribe(EventType::BLOCK_PLACE, [](Event& e) {
    auto& placeEvent = static_cast<BlockPlaceEvent&>(e);
    for (const auto& region : protectedRegions) {
        if (isInRegion(placeEvent.position, region)) {
            placeEvent.cancel();
            notifyPlayer(placeEvent.placerEntityID, "This area is protected!");
            break;
        }
    }
}, EventPriority::HIGHEST, "region_protection");
```

## 6.4 Engine API Reference

The `EngineAPI` class provides a high-level interface for interacting with the voxel engine. It's the primary API used by console commands, scripts, and external tools.

**Header:** `include/engine_api.h`

### Initialization

```cpp
// Get singleton instance
auto& api = EngineAPI::instance();

// Initialize with core engine components (called during engine startup)
api.initialize(world, renderer, player);

// Check if initialized
if (api.isInitialized()) {
    // Use API methods...
}
```

### Block Manipulation

#### Place Block

```cpp
// Place block by ID
bool placeBlock(int x, int y, int z, int blockID);
bool placeBlock(const glm::ivec3& pos, int blockID);

// Place block by name
bool placeBlock(const glm::ivec3& pos, const std::string& blockName);

// Example:
api.placeBlock(10, 20, 30, 5);                    // Place block ID 5
api.placeBlock(glm::ivec3(10, 20, 30), "stone");  // Place stone
```

**Returns:** `true` if successful, `false` if out of bounds or invalid block

#### Break Block

```cpp
// Remove a block
bool breakBlock(int x, int y, int z);
bool breakBlock(const glm::ivec3& pos);

// Example:
api.breakBlock(10, 20, 30);
```

**Returns:** `true` if successful, `false` if out of bounds

#### Block Metadata

```cpp
// Set block metadata (0-255)
bool setBlockMetadata(const glm::ivec3& pos, uint8_t metadata);

// Get block metadata
uint8_t getBlockMetadata(const glm::ivec3& pos);

// Example:
api.setBlockMetadata(glm::ivec3(10, 20, 30), 5);  // Set metadata to 5
uint8_t meta = api.getBlockMetadata(glm::ivec3(10, 20, 30));
```

#### Query Block

```cpp
// Get block information
BlockQueryResult getBlockAt(int x, int y, int z);
BlockQueryResult getBlockAt(const glm::ivec3& pos);

struct BlockQueryResult {
    bool valid;              // True if query succeeded
    int blockID;             // Block ID (0 = air)
    std::string blockName;   // Block name (e.g., "grass", "stone")
    glm::ivec3 position;     // Block position
};

// Example:
auto result = api.getBlockAt(10, 20, 30);
if (result.valid) {
    std::cout << "Block: " << result.blockName << " (ID: " << result.blockID << ")" << std::endl;
}
```

### Area Operations

#### Fill Area

```cpp
// Fill rectangular region with blocks
int fillArea(const glm::ivec3& start, const glm::ivec3& end, int blockID);
int fillArea(const glm::ivec3& start, const glm::ivec3& end, const std::string& blockName);

// Example:
int count = api.fillArea(
    glm::ivec3(0, 0, 0),      // Start corner
    glm::ivec3(10, 10, 10),   // End corner
    "stone"                   // Block type
);
std::cout << "Placed " << count << " blocks" << std::endl;
```

**Returns:** Number of blocks placed

#### Replace Blocks

```cpp
// Replace all blocks of one type with another in a region
int replaceBlocks(const glm::ivec3& start, const glm::ivec3& end,
                  int fromBlockID, int toBlockID);
int replaceBlocks(const glm::ivec3& start, const glm::ivec3& end,
                  const std::string& fromName, const std::string& toName);

// Example:
int count = api.replaceBlocks(
    glm::ivec3(0, 0, 0),
    glm::ivec3(50, 50, 50),
    "dirt",   // Replace all dirt
    "stone"   // With stone
);
std::cout << "Replaced " << count << " blocks" << std::endl;
```

**Returns:** Number of blocks replaced

### Sphere Operations

#### Fill Sphere

```cpp
// Fill spherical region with blocks
int fillSphere(const glm::vec3& center, float radius, int blockID);

// Example:
int count = api.fillSphere(
    glm::vec3(50.0f, 50.0f, 50.0f),  // Center
    10.0f,                            // Radius
    1                                 // Block ID (stone)
);
```

**Returns:** Number of blocks placed

#### Hollow Sphere

```cpp
// Create hollow sphere shell
int hollowSphere(const glm::vec3& center, float radius, int blockID, float thickness = 1.0f);

// Example:
int count = api.hollowSphere(
    glm::vec3(50.0f, 50.0f, 50.0f),  // Center
    15.0f,                            // Radius
    5,                                // Block ID (glass)
    2.0f                              // Shell thickness
);
```

**Returns:** Number of blocks placed

### Terrain Modification (Brush System)

All terrain brush operations use the `BrushSettings` struct:

```cpp
struct BrushSettings {
    float radius = 5.0f;      // Brush radius in blocks
    float strength = 1.0f;    // Brush strength (0.0 to 1.0)
    float falloff = 0.5f;     // Edge falloff (0 = hard, 1 = smooth)
    bool affectWater = false; // Affect water blocks
};
```

#### Raise Terrain

```cpp
// Raise terrain in circular area
int raiseTerrain(const glm::vec3& center, float radius, float height,
                 const BrushSettings& brush = {});

// Example:
BrushSettings brush;
brush.radius = 10.0f;
brush.strength = 1.0f;
brush.falloff = 0.7f;

int count = api.raiseTerrain(
    glm::vec3(50.0f, 50.0f, 50.0f),  // Center point
    10.0f,                            // Radius
    5.0f,                             // Raise by 5 blocks
    brush
);
```

#### Lower Terrain

```cpp
// Lower terrain in circular area
int lowerTerrain(const glm::vec3& center, float radius, float depth,
                 const BrushSettings& brush = {});

// Example:
int count = api.lowerTerrain(
    glm::vec3(50.0f, 50.0f, 50.0f),
    10.0f,   // Radius
    3.0f     // Lower by 3 blocks
);
```

#### Smooth Terrain

```cpp
// Smooth terrain by averaging heights
int smoothTerrain(const glm::vec3& center, float radius,
                  const BrushSettings& brush = {});

// Example:
int count = api.smoothTerrain(
    glm::vec3(50.0f, 50.0f, 50.0f),
    8.0f  // Smooth radius
);
```

#### Paint Terrain

```cpp
// Paint terrain surface with specific block type
int paintTerrain(const glm::vec3& center, float radius, int blockID,
                 const BrushSettings& brush = {});

// Example:
int count = api.paintTerrain(
    glm::vec3(50.0f, 50.0f, 50.0f),
    12.0f,  // Paint radius
    3       // Grass block ID
);
```

#### Flatten Terrain

```cpp
// Flatten terrain to specific Y level
int flattenTerrain(const glm::vec3& center, float radius, int targetY,
                   const BrushSettings& brush = {});

// Example:
int count = api.flattenTerrain(
    glm::vec3(50.0f, 50.0f, 50.0f),
    15.0f,  // Flatten radius
    64      // Target Y level (sea level)
);
```

### Structure Spawning

```cpp
// Spawn structure at position
bool spawnStructure(const std::string& name, const glm::ivec3& position);

// Example:
if (api.spawnStructure("oak_tree", glm::ivec3(50, 64, 50))) {
    std::cout << "Tree spawned!" << std::endl;
}
```

**Returns:** `true` if successful, `false` if structure not found

> **Note:** Rotation parameter exists but is not yet implemented.

### Entity/Mesh Spawning

```cpp
// Spawn entity information
struct SpawnedEntity {
    uint32_t entityID;       // Unique identifier
    glm::vec3 position;      // Position in world
    std::string type;        // Entity type
};
```

#### Spawn Primitives

```cpp
// Spawn sphere entity
SpawnedEntity spawnSphere(const glm::vec3& position, float radius,
                          const glm::vec4& color = glm::vec4(1.0f));

// Spawn cube entity
SpawnedEntity spawnCube(const glm::vec3& position, float size,
                        const glm::vec4& color = glm::vec4(1.0f));

// Spawn cylinder entity
SpawnedEntity spawnCylinder(const glm::vec3& position, float radius, float height,
                            const glm::vec4& color = glm::vec4(1.0f));

// Example:
auto sphere = api.spawnSphere(
    glm::vec3(50.0f, 70.0f, 50.0f),  // Position
    2.0f,                             // Radius
    glm::vec4(1.0f, 0.0f, 0.0f, 1.0f) // Red color
);
std::cout << "Spawned sphere with ID: " << sphere.entityID << std::endl;
```

#### Spawn Custom Mesh

```cpp
// Spawn mesh from OBJ file
SpawnedEntity spawnMesh(const std::string& meshName, const glm::vec3& position,
                        const glm::vec3& scale = glm::vec3(1.0f),
                        const glm::vec3& rotation = glm::vec3(0.0f));

// Example:
auto entity = api.spawnMesh(
    "tree_model",                     // Mesh file (tree_model.obj)
    glm::vec3(50.0f, 64.0f, 50.0f),  // Position
    glm::vec3(2.0f),                 // Scale 2x
    glm::vec3(0.0f, 45.0f, 0.0f)     // Rotate 45 degrees around Y
);
```

#### Entity Management

```cpp
// Remove entity
bool removeEntity(uint32_t entityID);

// Update entity properties
bool setEntityPosition(uint32_t entityID, const glm::vec3& position);
bool setEntityScale(uint32_t entityID, const glm::vec3& scale);
bool setEntityColor(uint32_t entityID, const glm::vec4& color);

// Get all entities
std::vector<SpawnedEntity> getAllEntities();

// Example:
api.setEntityPosition(sphere.entityID, glm::vec3(60.0f, 70.0f, 60.0f));
api.setEntityColor(sphere.entityID, glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));  // Change to green

if (api.removeEntity(sphere.entityID)) {
    std::cout << "Entity removed" << std::endl;
}
```

### World Queries

#### Raycast

```cpp
// Cast ray into world
RaycastResult raycast(const glm::vec3& origin, const glm::vec3& direction,
                      float maxDistance = 100.0f);

struct RaycastResult {
    bool hit;                // True if ray hit a block
    glm::vec3 position;      // Hit point position
    glm::vec3 normal;        // Hit face normal
    glm::ivec3 blockPos;     // Block coordinates
    int blockID;             // Block ID
    float distance;          // Distance to hit
};

// Example:
auto result = api.raycast(
    glm::vec3(50.0f, 70.0f, 50.0f),  // Origin
    glm::vec3(0.0f, -1.0f, 0.0f),    // Direction (down)
    100.0f                            // Max distance
);

if (result.hit) {
    std::cout << "Hit block " << result.blockID
              << " at distance " << result.distance << std::endl;
}
```

#### Get Blocks in Radius

```cpp
// Get all blocks within spherical radius
std::vector<BlockQueryResult> getBlocksInRadius(const glm::vec3& center, float radius);

// Example:
auto blocks = api.getBlocksInRadius(glm::vec3(50.0f, 64.0f, 50.0f), 5.0f);
std::cout << "Found " << blocks.size() << " blocks" << std::endl;

for (const auto& block : blocks) {
    if (block.blockID == 5) {  // Find all stone blocks
        std::cout << "Stone at: " << block.position.x << ", "
                  << block.position.y << ", " << block.position.z << std::endl;
    }
}
```

#### Get Blocks in Area

```cpp
// Get all blocks in rectangular region
std::vector<BlockQueryResult> getBlocksInArea(const glm::ivec3& start, const glm::ivec3& end);

// Example:
auto blocks = api.getBlocksInArea(
    glm::ivec3(0, 0, 0),
    glm::ivec3(10, 10, 10)
);
```

#### Biome and Terrain

```cpp
// Get biome name at horizontal position
std::string getBiomeAt(float x, float z);

// Get terrain height (Y coordinate of top block)
int getHeightAt(float x, float z);

// Example:
std::string biome = api.getBiomeAt(50.0f, 50.0f);
int height = api.getHeightAt(50.0f, 50.0f);
std::cout << "Biome: " << biome << ", Height: " << height << std::endl;
```

### Player Control

```cpp
// Get player eye position
glm::vec3 getPlayerPosition();

// Set player position
void setPlayerPosition(const glm::vec3& pos);

// Get player look direction (normalized)
glm::vec3 getPlayerLookDirection();

// Get block player is looking at
RaycastResult getPlayerTarget(float maxDistance = 5.0f);

// Example:
glm::vec3 pos = api.getPlayerPosition();
glm::vec3 dir = api.getPlayerLookDirection();

auto target = api.getPlayerTarget();
if (target.hit) {
    std::cout << "Looking at block " << target.blockID << std::endl;
}

// Teleport player
api.setPlayerPosition(glm::vec3(0.0f, 100.0f, 0.0f));
```

### Water Operations

```cpp
// Place water source block
bool placeWater(const glm::ivec3& pos);

// Remove water
bool removeWater(const glm::ivec3& pos);

// Flood fill area with blocks
int floodFill(const glm::ivec3& start, int blockID, int maxBlocks = 10000);

// Example:
api.placeWater(glm::ivec3(50, 64, 50));

// Fill connected air blocks with stone
int filled = api.floodFill(glm::ivec3(50, 65, 50), 1, 500);
std::cout << "Filled " << filled << " blocks" << std::endl;
```

### Utility Functions

```cpp
// Convert block name to ID
int getBlockID(const std::string& blockName);

// Convert block ID to name
std::string getBlockName(int blockID);

// Get all registered blocks
std::vector<std::string> getAllBlockNames();

// Get all registered structures
std::vector<std::string> getAllStructureNames();

// Get all registered biomes
std::vector<std::string> getAllBiomeNames();

// Example:
int stoneID = api.getBlockID("stone");
std::string name = api.getBlockName(5);

auto allBlocks = api.getAllBlockNames();
for (const auto& blockName : allBlocks) {
    std::cout << blockName << std::endl;
}
```

### Time Control

```cpp
// Get time of day (0.0 = midnight, 0.5 = noon, 1.0 = next midnight)
float getTimeOfDay();

// Set time of day
void setTimeOfDay(float time);

// Example:
api.setTimeOfDay(0.5f);   // Set to noon
api.setTimeOfDay(0.0f);   // Set to midnight
api.setTimeOfDay(0.75f);  // Set to sunset
```

## 6.5 Console Commands

The engine features a powerful Source Engine-style console accessible via **F9**. Commands support autocomplete (Tab key) and provide comprehensive world manipulation capabilities.

### General Commands

#### help
Show all available commands or detailed help for a specific command.
```
help               - List all commands
help <command>     - Show detailed help for command
```
**Examples:**
```
help
help tp
help api
```

#### clear
Clear the console output.
```
clear
```

#### echo
Print a message to the console.
```
echo <message>
```
**Example:**
```
echo Hello, World!
```

### Debug Commands

#### noclip
Toggle noclip mode (fly through walls).
```
noclip
```
**Controls in noclip:**
- Space - Fly up
- Shift - Fly down
- WASD - Move in any direction

#### wireframe
Toggle wireframe rendering mode.
```
wireframe
```

#### lighting
Toggle the voxel lighting system.
```
lighting
```
**Note:** Regenerate chunks (move around) to see the effect.

#### debug
Toggle debug rendering modes and performance monitoring.
```
debug render        - Toggle debug overlays
debug drawfps       - Toggle FPS counter
debug targetinfo    - Toggle block targeting info
debug perf [interval] - Toggle performance monitoring
```
**Examples:**
```
debug drawfps
debug perf 5.0      - Report every 5 seconds
```

### Movement Commands

#### tp (teleport)
Teleport player to coordinates.
```
tp <x> <y> <z>
```
**Example:**
```
tp 0 100 0          - Teleport to world spawn at Y=100
tp 500 64 -200      - Teleport to specific coordinates
```

### World Manipulation Commands

#### reload
Hot-reload assets from disk without restarting.
```
reload <all|blocks|structures|biomes>
```
**Examples:**
```
reload blocks       - Reload block definitions and regenerate chunks
reload structures   - Reload structure definitions
reload biomes       - Reload biome definitions
reload all          - Reload everything
```
**Use cases:**
- Modify block textures/properties in `assets/blocks/`
- Add new structures to `assets/structures/`
- Adjust biome settings in `assets/biomes/`
- See changes immediately without restarting

#### api
Engine API commands for block manipulation.
```
api place <blockName> <x> <y> <z>
api fill <blockName> <x1> <y1> <z1> <x2> <y2> <z2>
api sphere <blockName> <x> <y> <z> <radius>
api replace <fromBlock> <toBlock> <x1> <y1> <z1> <x2> <y2> <z2>
```
**Examples:**
```
api place stone 10 64 20
api fill grass 0 64 0 10 64 10
api sphere stone 50 70 50 5
api replace dirt stone 0 0 0 100 100 100
```

#### brush
Terrain brush tools (operates on targeted block).
```
brush raise <radius> <height>
brush lower <radius> <depth>
brush smooth <radius>
brush paint <blockName> <radius>
brush flatten <radius> [targetY]
```
**How to use:**
1. Point crosshair at terrain
2. Run brush command
3. Terrain is modified at the targeted location

**Examples:**
```
brush raise 10 5       - Raise terrain in 10-block radius by 5 blocks
brush lower 8 3        - Lower terrain in 8-block radius by 3 blocks
brush smooth 12        - Smooth terrain in 12-block radius
brush paint grass 15   - Paint grass in 15-block radius
brush flatten 20 64    - Flatten to Y=64 in 20-block radius
```

#### spawn
Spawn entities in the world (at targeted location).
```
spawn sphere <radius> [r] [g] [b]
spawn cube <size> [r] [g] [b]
spawn cylinder <radius> <height> [r] [g] [b]
```
**Note:** Entity system is currently in development.

**Examples:**
```
spawn sphere 2 1.0 0.0 0.0      - Red sphere, radius 2
spawn cube 3 0.0 1.0 0.0        - Green cube, size 3
spawn cylinder 1 5 0.0 0.0 1.0  - Blue cylinder
```

#### entity
Entity management commands.
```
entity list                - List all spawned entities
entity remove <id>         - Remove entity by ID
entity clear               - Remove all entities
```
**Note:** Entity system is currently in development.

#### spawnstructure
Spawn a structure at the targeted ground position.
```
spawnstructure <name>
spawnstructure            - List available structures
```
**How to use:**
1. Point crosshair at ground
2. Run command
3. Structure spawns on top of targeted block

**Examples:**
```
spawnstructure oak_tree
spawnstructure house
spawnstructure            - Show all available structures
```

### Time Commands

#### skytime
Set the time of day.
```
skytime <0-1>
skytime               - Show current time
```
**Time values:**
- 0.0 = Midnight
- 0.25 = Sunrise
- 0.5 = Noon
- 0.75 = Sunset
- 1.0 = Next midnight

**Examples:**
```
skytime 0.5          - Set to noon
skytime 0.0          - Set to midnight
skytime              - Display current time
```

#### timespeed
Set time progression speed.
```
timespeed <value>
timespeed            - Show current speed
```
**Speed values:**
- 0 = Paused (time frozen)
- 1 = Normal (20 minutes per day cycle)
- 10 = 10x faster
- 100 = 100x faster

**Examples:**
```
timespeed 0          - Pause time
timespeed 1          - Normal speed
timespeed 10         - Fast forward 10x
```

### Console Variables (ConVars)

#### set
Set a ConVar value.
```
set <name> <value>
```
**Example:**
```
set fov 90
set renderDistance 10
```

#### get
Get a ConVar value and description.
```
get <name>
```
**Example:**
```
get fov
```

#### list
List all console variables with their values and flags.
```
list
```
**Flags:**
- [ARCHIVE] - Saved to config.ini
- [CHEAT] - Requires cheats enabled

### Command Autocomplete

Press **Tab** to autocomplete commands and arguments:
- Command names
- Block names (for api, brush, etc.)
- Structure names (for spawnstructure)
- ConVar names (for set, get)

**Example workflow:**
```
api pl<Tab>          → api place
api place sto<Tab>   → api place stone
```

## 6.6 YAML Scripting System

The engine features a comprehensive YAML-based scripting system that allows content creators to define block behaviors and event responses without writing C++ code. This system is **fully implemented** and ready for use.

### Files
- `include/script_action.h` - Action type definitions and parsing
- `src/script_action.cpp` - YAML parsing and action execution
- `assets/blocks/*.yaml` - Block definitions with event handlers

### YAML Event Syntax

Block definitions support event handlers directly in YAML:

```yaml
id: 50
name: "Pressure Plate"
texture: "pressure_plate.png"
durability: 1
affected_by_gravity: false

# Event handlers - WORKING FEATURE!
events:
  on_step:
    - type: run_command
      command: "echo Stepped on pressure plate!"
      probability: 100
    - type: spawn_particles
      particle: "redstone_dust"
      offset: [0, 1, 0]
      probability: 100

  on_neighbor_change:
    - type: break_block
      offset: [0, 0, 0]
      probability: 50

  on_place:
    - type: run_command
      command: "echo Pressure plate placed!"
      probability: 100
```

### Supported Event Types

The following event types can be used in block YAML files:

| Event Type | Trigger | Use Cases |
|------------|---------|-----------|
| `on_break` | Block is broken | Explosions, drops, chain reactions |
| `on_place` | Block is placed | Initialization, validation effects |
| `on_step` | Entity steps on block | Pressure plates, damage floors |
| `on_neighbor_change` | Adjacent block changes | Torches falling, water flow |
| `on_interact` | Player right-clicks | Buttons, levers, GUIs |
| `on_update` | Scheduled tick fires | Crop growth, random effects |

### Implemented Action Types

**place_block** - Place a block at offset position
```yaml
- type: place_block
  block: "Stone"           # Block name (required)
  offset: [1, 0, 0]        # Relative position (optional, default: [0,0,0])
  probability: 75          # Chance to execute (optional, default: 100)
```

**break_block** - Break block at offset position
```yaml
- type: break_block
  offset: [-1, 0, 0]       # Relative position (optional)
  probability: 50          # Chance to execute (optional)
```

**spawn_structure** - Spawn a structure at offset position
```yaml
- type: spawn_structure
  structure: "Oak Tree"    # Structure name (required)
  offset: [0, 1, 0]        # Relative position (optional)
  probability: 25          # Chance to execute (optional)
```

**spawn_particles** - Spawn particle effects (TODO: renderer integration)
```yaml
- type: spawn_particles
  particle: "explosion"    # Particle effect name (required)
  offset: [0, 0, 0]        # Relative position (optional)
  probability: 100         # Chance to execute (optional)
```

**play_sound** - Play sound effect (FUTURE: needs audio system)
```yaml
- type: play_sound
  sound: "click.wav"       # Sound file name (required)
  probability: 100         # Chance to execute (optional)
```

**run_command** - Execute console command
```yaml
- type: run_command
  command: "echo Hello!"   # Command string (required)
  probability: 100         # Chance to execute (optional)
```

**set_metadata** - Set block metadata values
```yaml
- type: set_metadata
  metadata:
    water_level: "5"       # Key-value pairs
    state: "active"
  offset: [0, 0, 0]        # Relative position (optional)
  probability: 100         # Chance to execute (optional)
```

**trigger_update** - Schedule a block update tick
```yaml
- type: trigger_update
  offset: [0, 0, 0]        # Relative position (optional)
  probability: 100         # Chance to execute (optional)
```

**conditional** - Execute actions based on conditions
```yaml
- type: conditional
  condition: "random_chance"  # Condition type (see table below)
  condition_value: "50"       # For random_chance: probability 0-100
  then:                       # Actions if condition is true
    - type: place_block
      block: "Gold"
      offset: [0, 1, 0]
  else:                       # Actions if condition is false (optional)
    - type: run_command
      command: "echo No luck!"
```

**Condition Types:**
| Condition | `condition_value` | Description |
|-----------|-------------------|-------------|
| `random_chance` | `"0-100"` | Random probability (integer percent) |
| `block_is` | `"Block Name"` | True if block at offset matches |
| `block_is_not` | `"Block Name"` | True if block at offset doesn't match |
| `time_is_day` | - | True during daytime (0-12000 ticks) |
| `time_is_night` | - | True during nighttime (12000-24000 ticks) |

**set_variable** - Store a script variable
```yaml
- type: set_variable
  variable: "counter"      # Variable name (required)
  value: "10"              # Value to store (required)
```

**increment_var** / **decrement_var** - Modify numeric variable
```yaml
- type: increment_var
  variable: "counter"      # Variable name (required)
  amount: 1                # Amount to add/subtract (optional, default: 1)
```

### Complete Block Example: Explosive Ore

```yaml
id: 51
name: "Explosive Ore"
texture: "explosive_ore.png"
durability: 5
affected_by_gravity: false
flammability: 0
transparency: 0.0

events:
  on_break:
    # Spawn explosion particles
    - type: spawn_particles
      particle: "explosion"
      offset: [0, 0, 0]
      probability: 100

    # Break surrounding blocks (simulating explosion)
    - type: break_block
      offset: [1, 0, 0]
      probability: 75

    - type: break_block
      offset: [-1, 0, 0]
      probability: 75

    - type: break_block
      offset: [0, 1, 0]
      probability: 75

    - type: break_block
      offset: [0, -1, 0]
      probability: 75

    - type: break_block
      offset: [0, 0, 1]
      probability: 75

    - type: break_block
      offset: [0, 0, -1]
      probability: 75

    # Notify nearby blocks to update
    - type: trigger_update
      offset: [1, 0, 0]
      probability: 100

    # Log the explosion
    - type: run_command
      command: "echo Explosive ore detonated!"
      probability: 100

  on_neighbor_change:
    # 5% chance to explode from nearby explosions
    - type: break_block
      offset: [0, 0, 0]
      probability: 5
```

### Probability System

Every action supports a `probability` field (0-100):
- **100** (default) - Always executes
- **75** - 75% chance to execute
- **50** - 50% chance to execute
- **0** - Never executes

Probabilities are evaluated independently for each action, allowing for varied outcomes.

### Biome Event Handlers

Biomes can also have event handlers defined in YAML:

```yaml
name: "Magical Forest"
temperature: 0.7
humidity: 0.8

events:
  on_enter:
    - type: run_command
      command: "echo Welcome to the Magical Forest!"
    - type: spawn_particles
      particle: "sparkle"
      offset: [0, 2, 0]

  on_exit:
    - type: run_command
      command: "echo Leaving the forest..."

  on_chunk_generate:
    - type: conditional
      condition: "random_chance"
      condition_value: "10"
      then:
        - type: spawn_structure
          structure: "Fairy Ring"
          offset: [8, 0, 8]
```

**Biome Event Types:**
| Event | Trigger | Use Cases |
|-------|---------|-----------|
| `on_enter` | Player enters biome | Welcome messages, ambient effects |
| `on_exit` | Player leaves biome | Farewell effects, state cleanup |
| `on_chunk_generate` | Chunk generated in biome | Special structures, decorations |

### Script Variables

The ScriptVariableRegistry provides global state storage for scripts:

```yaml
# Example: Random reward on step
events:
  on_step:
    - type: increment_var
      variable: "steps_taken"
      amount: 1
    - type: conditional
      condition: "random_chance"
      condition_value: "10"
      then:
        - type: run_command
          command: "echo Lucky step!"
        - type: spawn_particles
          particle: "sparkle"
```

**Available Variable Actions:**
- `set_variable` - Set a variable to a specific value
- `get_variable` - Retrieve a variable's value (for conditions)
- `increment_var` - Add to a numeric variable
- `decrement_var` - Subtract from a numeric variable

All variables are stored as strings and converted to numbers for increment/decrement operations.

### Best Practices

1. **Keep events simple** - Complex logic belongs in C++ code via EngineAPI
2. **Use probability** - Add randomness for more natural-feeling effects
3. **Chain reactions** - Use `trigger_update` and `on_neighbor_change` for cascading effects
4. **Test thoroughly** - Event interactions can have unexpected results
5. **Document your YAML** - Add comments to explain complex behavior
6. **Performance awareness** - High-frequency events should be lightweight
7. **Use conditionals wisely** - Avoid deeply nested conditional trees
8. **Name variables clearly** - Use descriptive names like `block_50_counter` not `x`

## 6.7 Threading Model

Understanding the engine's threading model is crucial for proper event handling and API usage.

### Thread Architecture

The voxel engine uses a multi-threaded architecture with distinct thread responsibilities:

```
┌─────────────────────────────────────────────────────────────┐
│                        Main Thread                          │
│  • Game loop                                                │
│  • Input processing                                         │
│  • Player physics                                           │
│  • Vulkan rendering                                         │
│  • GPU buffer uploads                                       │
│  • ImGui UI                                                 │
│  • Main thread event queue processing                       │
└─────────────────────────────────────────────────────────────┘
                              │
                              ├─────────────────────┐
                              │                     │
┌─────────────────────────────▼─┐   ┌───────────────▼──────────┐
│   Event Handler Thread        │   │  World Streaming Threads  │
│  • Async event queue          │   │  • Chunk generation       │
│  • Event dispatch             │   │  • Chunk decoration       │
│  • Listener invocation        │   │  • Parallel meshing       │
│  • Event filtering            │   │  • Lighting propagation   │
└───────────────────────────────┘   └──────────────────────────┘
```

### Main Thread

**Responsibilities:**
- Game loop at 60 FPS target
- Vulkan rendering commands
- GPU buffer creation and uploads
- ImGui rendering
- Input handling
- Main thread event processing

**Thread Safety:**
- All Vulkan operations MUST run on main thread
- GPU buffer operations (create, upload, destroy) are main-thread only
- ImGui calls are main-thread only

**Main Thread Event Queue:**
```cpp
// Queue event for main thread processing
auto event = std::make_unique<ChunkLoadEvent>(0, 0, 0, true);
EventDispatcher::instance().queueForMainThread(std::move(event));

// Process queue (called once per frame in main loop)
EventDispatcher::instance().processMainThreadQueue();
```

**Use cases for main thread queue:**
- GPU buffer creation after chunk mesh generation
- Vulkan state changes triggered by events
- ImGui updates from event handlers

### Event Handler Thread

**Responsibilities:**
- Process async event queue
- Invoke event listeners in priority order
- Handle event cancellation
- Filter events

**Lifecycle:**
```cpp
// Start event handler thread (during engine initialization)
EventDispatcher::instance().start();

// Stop event handler thread (during engine shutdown)
EventDispatcher::instance().stop();  // Processes remaining events before stopping
```

**Thread Safety:**
- Event queue is protected by mutex
- Listener list is protected by mutex
- Events are processed sequentially on handler thread
- Multiple listeners can process same event

**Event Processing Flow:**
1. Event queued via `dispatch()`
2. Handler thread dequeues event
3. Listeners invoked in priority order (HIGHEST → LOWEST)
4. MONITOR listeners called last (cannot cancel)
5. Event marked as processed

### World Streaming Threads

**Responsibilities:**
- Parallel chunk generation (multiple chunks simultaneously)
- Parallel chunk decoration (trees, structures)
- Parallel mesh generation (thread-safe with shared_lock)
- Lighting propagation (batched for performance)

**Thread Safety:**
- World chunk map uses `std::shared_mutex` for concurrent reads
- Mesh generation uses `shared_lock` (multiple readers, one writer)
- Chunk decoration spawns thread per chunk
- Staging buffer pool is mutex-protected

**Performance Optimizations:**
- **Parallel decorations** - All chunks decorated simultaneously (3× faster)
- **Parallel mesh generation** - All chunks meshed simultaneously (5× faster)
- **Batched GPU uploads** - Single vkQueueSubmit for multiple chunks (90% overhead reduction)

### Thread Safety Guidelines

#### Safe Operations from Any Thread

✅ **EngineAPI methods** - All thread-safe with internal locking:
```cpp
// Safe to call from any thread
api.placeBlock(10, 20, 30, 5);
api.fillSphere(center, radius, blockID);
auto result = api.raycast(origin, direction);
```

✅ **Event dispatching**:
```cpp
// Safe to dispatch from any thread
dispatcher.dispatch(std::make_unique<BlockBreakEvent>(...));
dispatcher.dispatchImmediate(event);  // Processes on calling thread
```

✅ **World queries** (read-only):
```cpp
// Safe with shared_lock
int blockID = world->getBlockAt(x, y, z);
```

#### Main Thread Only

❌ **Vulkan operations**:
```cpp
// MUST be on main thread
renderer->createVertexBuffer(...);
renderer->beginFrame();
renderer->renderChunk(chunk);
renderer->endFrame();
```

❌ **ImGui calls**:
```cpp
// MUST be on main thread
ImGui::Begin("Window");
ImGui::Text("Hello");
ImGui::End();
```

❌ **GPU buffer management**:
```cpp
// MUST be on main thread
chunk->createVertexBuffer(renderer);
chunk->destroyBuffers();
```

### Event Handling Patterns

#### Pattern 1: Async Event, Main Thread Action

Event triggered on any thread, action requires main thread:

```cpp
// Event triggered from any thread
dispatcher.subscribe(EventType::CHUNK_LOAD, [](Event& e) {
    auto& chunkEvent = static_cast<ChunkLoadEvent&>(e);

    // Do non-GPU work here (safe on event thread)
    processChunkData(chunkEvent.chunkX, chunkEvent.chunkY, chunkEvent.chunkZ);

    // Queue GPU work for main thread
    auto gpuEvent = std::make_unique<CustomEvent>("chunk_gpu_upload");
    gpuEvent->data = std::make_any<ChunkCoord>(chunkEvent.chunkX,
                                                chunkEvent.chunkY,
                                                chunkEvent.chunkZ);
    EventDispatcher::instance().queueForMainThread(std::move(gpuEvent));
}, EventPriority::NORMAL, "chunk_loader");

// Main thread handler for GPU upload
dispatcher.subscribe(EventType::CUSTOM, [](Event& e) {
    auto& customEvent = static_cast<CustomEvent&>(e);
    if (customEvent.eventName == "chunk_gpu_upload") {
        auto coord = std::any_cast<ChunkCoord>(customEvent.data);
        // Safe to do GPU work here (main thread)
        uploadChunkToGPU(coord);
    }
}, EventPriority::NORMAL, "gpu_uploader");
```

#### Pattern 2: Immediate Event for Time-Sensitive Operations

When event must be processed synchronously:

```cpp
// Create and dispatch immediately (blocks until all listeners processed)
BlockBreakEvent event(position, blockID, BreakCause::PLAYER, playerID);
dispatcher.dispatchImmediate(event);

// Check if any listener cancelled the event
if (!event.isCancelled()) {
    // Proceed with breaking the block
    world->setBlockAt(position.x, position.y, position.z, 0);
}
```

#### Pattern 3: High-Frequency Events with Filtering

Avoid processing every event when only specific cases matter:

```cpp
// Only process grass block breaks
dispatcher.subscribeFiltered(
    EventType::BLOCK_BREAK,
    [](Event& e) {
        auto& breakEvent = static_cast<BlockBreakEvent&>(e);
        // This only runs for grass blocks
        dropGrassSeeds(breakEvent.position);
    },
    [](const Event& e) {
        auto& breakEvent = static_cast<const BlockBreakEvent&>(e);
        return breakEvent.blockID == 3;  // Grass block ID
    },
    EventPriority::NORMAL,
    "grass_drops"
);
```

### Performance Considerations

**Event Dispatch Performance:**
- Async dispatch: ~100-500 ns (just queue + notify)
- Sync dispatch: Depends on listener count and complexity
- Main thread queue: Processed once per frame (16.7ms budget @ 60 FPS)

**Listener Count Impact:**
- Each listener adds overhead to event processing
- Use filtered listeners to reduce unnecessary invocations
- MONITOR priority adds minimal overhead (no cancellation checks)

**Memory Usage:**
- Event queue dynamically sized
- Listeners stored in hash map (O(1) lookup by event type)
- Filtered listeners have separate storage

**Best Practices:**
1. **Use async dispatch** when possible (better performance)
2. **Use filtered listeners** for high-frequency events
3. **Keep listeners fast** - Offload heavy work to separate threads
4. **Queue GPU work** - Never block event thread on GPU operations
5. **Use MONITOR** - For logging/stats that shouldn't affect gameplay
6. **Unsubscribe properly** - Clean up listeners when systems shut down

---

# 7. Performance & Optimization

## 7.1 Current Performance

**Benchmarks:**
- Startup Time: ~5 seconds (432 chunks)
- GPU Sync Points: 1 per frame (was 432)
- Chunk Generation: ~10ms average
- Mesh Generation: <1ms per chunk
- Vertex Reduction: 50-80% (greedy meshing)
- Memory Usage: ~14MB full world, ~2.1GB streamed
- FPS: 60+ stable

**Major Optimizations Implemented:**

**Terrain Generation (2025-11-20):**
1. Recursive Terrain Height Fix (32x reduction, 2-3x speedup)
2. Mountain Density Caching (99.9% noise reduction, 5-8x speedup)
3. Chunk Init with memset (10-20x faster initialization)
4. sqrt Elimination in Trees (2-3x faster canopy generation)
5. Thread-Local RNG (2-4x faster parallel decoration)

**Rendering & GPU:**
6. GPU Upload Batching (10-15x sync reduction)
7. Greedy Meshing (50-80% vertex reduction)
8. Mesh Buffer Pooling (40-60% speedup)
9. GPU Buffer Deletion Rate Limiting (eliminates 600ms stalls)
10. GPU Warm-Up Phase (instant 60 FPS gameplay start)
11. Dedicated Transfer Queue (eliminates edge lag - parallel with rendering)
12. VK_SHARING_MODE_CONCURRENT (avoids queue ownership transfer overhead)
13. Persistent Staging Buffer Mapping (eliminates vkMapMemory/vkUnmapMemory overhead)
14. Memory Barriers for Transfer Sync (proper visibility guarantees)

**Storage & Streaming:**
11. Chunk Compression (80-95% disk space savings)
12. Async World Streaming (no frame stuttering)
13. Thread-safe Chunk Access (proper locking)
14. Chunk Loading Lock Optimization (1,331 → 1 lock acquisition)
15. Zero-Copy Chunk Iteration (callback pattern, no vector copying)
16. World Loading Fix (chunk discovery from disk)

**Combined Impact:** 4-8x faster initial world generation, instant 60 FPS gameplay

## 7.2 Profiling

### Built-in Profiling

**FPS Counter:**
```
debug drawfps
```

**Target Information:**
```
debug targetinfo
```

**Render Statistics:**
```
debug render
```

### External Tools

**Windows:**
- Visual Studio Profiler
- Intel VTune
- NVIDIA Nsight

**Linux:**
- perf
- Valgrind (cachegrind, callgrind)
- gperftools

**GPU Profiling:**
- RenderDoc (frame debugging)
- NVIDIA Nsight Graphics
- AMD Radeon GPU Profiler

## 7.3 Optimization Checklist

### CPU Optimization

- [ ] Use greedy meshing for all chunks
- [ ] Implement mesh buffer pooling
- [ ] Cache chunk neighbors for mesh generation
- [ ] Use thread-local allocators
- [ ] Minimize mutex lock duration
- [ ] Batch similar operations
- [ ] Use move semantics where possible

### GPU Optimization

- [ ] Batch vertex buffer uploads
- [ ] Use single texture atlas
- [ ] Implement frustum culling
- [ ] Use indexed rendering
- [ ] Minimize pipeline state changes
- [ ] Use appropriate buffer usage flags
- [ ] Implement occlusion culling (future)

### Memory Optimization

- [x] Implement chunk pooling (acquireChunk/releaseChunk in World)
- [x] Implement mesh buffer pooling (MeshBufferPool class)
- [x] Compress chunks on disk (RLE compression in chunk save/load)
- [x] Unload distant chunks (WorldStreaming with unload distance)
- [ ] Clear unused vertex data
- [x] Use appropriate data types (uint8_t vs int)
- [ ] Monitor memory leaks with tools

---

# 8. Troubleshooting

## 8.1 Build Issues

### "VULKAN_SDK environment variable not found"

**Solution:**
1. Download Vulkan SDK from https://vulkan.lunarg.com/
2. Install with "Shader Toolchain" option
3. **Restart computer** (important!)
4. Verify: `echo %VULKAN_SDK%` (Windows) or `echo $VULKAN_SDK` (Linux)

### "Visual Studio not detected" (Windows)

**Solution:**
- Install Visual Studio 2019 or 2022
- Select "Desktop development with C++" workload
- Or install Visual Studio Build Tools

### "No shader compiler found"

**Solution:**
- Reinstall Vulkan SDK
- Ensure "Shader Toolchain" is checked
- Manually compile: `glslc shader.vert -o vert.spv`

### "Missing required libraries: glfw3" (Linux)

**Solution:**
```bash
# Ubuntu/Debian
sudo apt install libglfw3-dev

# Arch Linux
sudo pacman -S glfw
```

## 8.2 Runtime Issues

### Black screen or crash on startup

**Possible Causes:**
1. Shaders not compiled
2. Graphics drivers outdated
3. GPU doesn't support Vulkan

**Solutions:**
- Compile shaders: `cd shaders && compile.bat` (Windows) or `./compile.sh` (Linux)
- Update graphics drivers from manufacturer website
- Check Vulkan support: `vulkaninfo`

### Low FPS or stuttering

**Possible Causes:**
1. Running in Debug mode
2. Too many chunks loaded
3. Validation layers enabled

**Solutions:**
- Build in Release mode
- Reduce render distance in config
- Disable validation layers (Release build)
- Update graphics drivers
- Check GPU temperature

### Memory leaks

**Diagnosis:**
```bash
# Linux
valgrind --leak-check=full ./voxel-engine

# Windows
# Use Visual Studio diagnostic tools
```

**Common Causes:**
- Not clearing vertex buffers after upload
- Chunks not properly unloaded
- GPU resources not destroyed

**Solutions:**
- Ensure `cleanup()` methods are called
- Implement proper RAII
- Use smart pointers where appropriate

### Crashes when breaking/placing blocks

**Possible Causes:**
1. Accessing chunks outside bounds
2. Race conditions in chunk access
3. Null pointer dereferences

**Solutions:**
- Check bounds before block access
- Ensure proper mutex locking
- Validate chunk pointers before use

### Console won't open

**Solution:**
- Check F9 key binding in config.ini
- Ensure console is initialized
- Check for conflicting input handlers

## 8.3 Common Errors

### "Failed to create Vulkan instance"

**Cause:** Vulkan not properly installed or GPU doesn't support Vulkan

**Solution:**
1. Install/reinstall Vulkan SDK
2. Update graphics drivers
3. Check `vulkaninfo` output

### "Validation layer error: ..."

**Cause:** Vulkan validation layers detected an issue

**Solution:**
- These are warnings in Debug builds
- Fix the underlying issue (check error message)
- Build in Release to disable validation

### "Chunk generation timeout"

**Cause:** Worker threads stuck or deadlocked

**Solution:**
- Check for deadlocks in thread code
- Ensure proper mutex unlock
- Verify condition variables are signaled

### "Out of memory"

**Cause:** Too many chunks loaded

**Solution:**
- Reduce render distance
- Implement proper chunk unloading
- Enable chunk compression
- Add more RAM

## 8.4 Debug Commands

Useful console commands for debugging:

```
# Performance
debug drawfps              # Show FPS
debug render               # Show render stats

# Position
tp 0 100 0                 # Teleport to known good location
get player_pos             # Get current position

# Rendering
wireframe                  # Toggle wireframe mode
noclip                     # Toggle noclip for exploring

# Lighting
lighting                   # Toggle lighting system
skytime 0.5                # Set to noon (full brightness)

# World
spawnstructure test        # Test structure spawning
set chunk_cache_size 1000  # Adjust chunk cache
```

## 8.5 Getting Help

**Documentation:**
- Check this handbook first
- Read specific system docs in `docs/`
- In-game: type `docs/<filename>.md` in console

**Community:**
- GitHub Issues: Report bugs and request features
- Discussions: Ask questions and share creations

**Debugging Tips:**
1. Enable validation layers (Debug build)
2. Use verbose logging
3. Test with minimal config
4. Isolate the problem (binary search)
5. Check logs in console output
6. Use debugger breakpoints

---

# Appendix A: Console Commands Reference

## Core Commands

| Command | Arguments | Description |
|---------|-----------|-------------|
| `help` | `[command]` | Show all commands or help for specific command |
| `clear` | - | Clear console output |
| `echo` | `<message>` | Print message to console |
| `quit` | - | Exit the game |

## World Commands

| Command | Arguments | Description |
|---------|-----------|-------------|
| `tp` | `<x> <y> <z>` | Teleport to coordinates |
| `spawnstructure` | `<name>` | Spawn structure at target position |
| `lighting` | - | Toggle lighting system |

## Player Commands

| Command | Arguments | Description |
|---------|-----------|-------------|
| `noclip` | - | Toggle noclip mode |

## Rendering Commands

| Command | Arguments | Description |
|---------|-----------|-------------|
| `wireframe` | - | Toggle wireframe rendering |
| `debug` | `<option>` | Toggle debug overlays (render/drawfps/targetinfo) |

## Time Commands

| Command | Arguments | Description |
|---------|-----------|-------------|
| `skytime` | `<0-1>` | Set time of day (0=midnight, 0.5=noon) |
| `timespeed` | `<value>` | Set time progression speed (0=pause, 1=normal) |

## ConVar Commands

| Command | Arguments | Description |
|---------|-----------|-------------|
| `set` | `<name> <value>` | Set console variable value |
| `get` | `<name>` | Get console variable value |
| `list` | - | List all console variables |

---

# Appendix B: Configuration Reference

## config.ini Format

```ini
[World]
seed = 1124                    # World generation seed

[Window]
width = 1920                   # Window width
height = 1080                  # Window height
fullscreen = false             # Fullscreen mode

[Controls]
mouse_sensitivity = 0.002      # Mouse look sensitivity
sprint_speed = 1.5             # Sprint speed multiplier

[Graphics]
vsync = true                   # V-Sync enabled
render_distance = 13           # Chunk render radius
fov = 70.0                     # Field of view

[Performance]
worker_threads = 4             # Number of chunk gen threads
mesh_pool_size = 100           # Mesh buffer pool size
chunk_cache_size = 2500        # Maximum cached chunks

[Debug]
show_fps = false               # Show FPS counter on startup
validation_layers = false      # Vulkan validation layers
```

---

# Appendix C: File Formats

## Block Definition (.yaml)

Blocks are defined in YAML files placed in `assets/blocks/`. Each block can have textures, gameplay properties, and liquid physics.

### Basic Example

```yaml
id: 10                        # OPTIONAL: Block ID (omit for auto-assignment)
name: "Stone Brick"           # REQUIRED: Display name
texture: "stone_brick.png"    # Simple texture (all faces)
durability: 5                 # Break resistance
affected_by_gravity: false    # Falls like sand if true
flammability: 0               # Fire spread rate (0 = fireproof)
transparency: 0.0             # 0.0 = opaque, 1.0 = fully transparent
liquid: false                 # Is this block a liquid?
```

### Block ID Assignment

- **Explicit ID**: Specify `id: 10` for guaranteed stable IDs (recommended for world-gen blocks)
- **Auto-Assigned ID**: Omit `id` field for automatic assignment starting from highest explicit ID + 1
- **Reserved IDs**: 0 = Air (cannot be changed)

### Texture Options

**Simple Texture (all faces same):**
```yaml
texture: "myblock.png"        # PNG file in assets/blocks/
# OR
texture: "#FF5733"            # Hex color code (RGB)
```

**Cube Map (per-face textures):**
```yaml
cube_map:
  all: "default.png"          # Default for unspecified faces
  top: "top.png"              # +Y face
  bottom: "bottom.png"        # -Y face
  front: "front.png"          # -Z face
  back: "back.png"            # +Z face
  left: "left.png"            # -X face
  right: "right.png"          # +X face
```

### Gameplay Properties

```yaml
durability: 5                 # Break time (0 = instant, higher = harder)
affected_by_gravity: false    # Falls when unsupported (like sand)
flammability: 0               # Fire spread rate (0 = fireproof, 100 = very flammable)
transparency: 0.0             # Visual transparency (0.0-1.0)
liquid: false                 # Liquid physics enabled
animated_tiles: 1             # Animation frames (1 = static, 2+ = animated)
```

### Biome/Climate Restrictions

**IMPORTANT:** Blocks MUST specify ALL four climate properties to spawn naturally. If omitted, the block will NOT spawn during world generation.

```yaml
min_temperature: 0            # Minimum biome temperature (0-100)
max_temperature: 100          # Maximum biome temperature (0-100)
min_moisture: 0               # Minimum biome moisture (0-100)
max_moisture: 100             # Maximum biome moisture (0-100)
```

**Rules:**
- If ALL climate properties are omitted: Block does NOT spawn naturally (player-placed only)
- If specified: Block spawns in biomes matching the temperature/moisture ranges

**EXCEPTION - Biome Override (Ultimate Call):**
Blocks explicitly specified in a biome's definition ALWAYS spawn in that biome, regardless of climate properties:
- `primary_surface_block` - Ground layer blocks (e.g., grass, sand, snow)
- `primary_stone_block` - Underground blocks (e.g., stone, sandstone)
- `primary_log_block` - Tree trunk blocks
- `primary_leave_block` - Tree foliage blocks

These biome-mandated blocks override climate restrictions. A block can have no climate properties but still spawn if a biome explicitly requires it.

### Liquid Properties

For liquid blocks, define underwater visual effects:

```yaml
liquid: true
liquid_properties:
  fog_color: [0.1, 0.3, 0.5]      # RGB fog color underwater (0.0-1.0)
  fog_density: 0.8                # Fog density (0.0-1.0)
  fog_start: 1.0                  # Distance where fog begins
  fog_end: 8.0                    # Distance where fog is fully opaque
  tint_color: [0.4, 0.7, 1.0]     # RGB tint when submerged
  darken_factor: 0.4              # Darkening multiplier (0.0-1.0)
```

### Custom Metadata

Use `metadata` for custom properties:

```yaml
metadata:
  harvest_tool: "pickaxe"
  harvest_level: 2
  drop_item: "cobblestone"
  custom_flag: true
```

### Complete Example

```yaml
id: 5
name: "Water"
texture: "water.png"
durability: 0
affected_by_gravity: false
flammability: 0
transparency: 0.6
liquid: true
animated_tiles: 4
min_temperature: 0
max_temperature: 100
min_moisture: 0
max_moisture: 100
liquid_properties:
  fog_color: [0.1, 0.3, 0.5]
  fog_density: 0.8
  fog_start: 1.0
  fog_end: 8.0
  tint_color: [0.4, 0.7, 1.0]
  darken_factor: 0.4
```

## Biome Definition (.yaml)

Biomes control terrain generation, vegetation, structures, and **block compatibility**. Place YAML files in `assets/biomes/`.

### Required Properties

```yaml
name: "grasslands"            # Biome identifier (lowercase, underscores)
temperature: 60               # 0-100 (0=coldest, 100=hottest)
moisture: 50                  # 0-100 (0=driest, 100=wettest)
age: 40                       # 0-100 (0=mountains, 100=flat plains)
activity: 70                  # 0-100 (structure/settlement spawn rate)
```

### Spawning and Generation

```yaml
spawn_location: "AboveGround"  # "Underground" / "AboveGround" / "Both"
lowest_y: 0                    # Minimum Y level for biome
underwater_biome: false        # Can spawn as ocean floor
river_compatible: true         # Can rivers cut through
biome_rarity_weight: 50        # 1-100 (higher = more common)
parent_biome: "plains"         # Inherit from parent biome
height_multiplier: 1.0         # Terrain height scale (2.0 = double height)
```

### Vegetation

```yaml
trees_spawn: true             # Enable tree generation
tree_density: 50              # 0-100 tree spawn rate
vegetation_density: 50        # 0-100 grass/flowers/mushrooms rate
```

### Block Compatibility (Block Control)

**These properties control which blocks can spawn in this biome:**

```yaml
required_blocks: "6,7"         # Comma-separated block IDs that MUST spawn
blacklisted_blocks: "4,5"      # Comma-separated block IDs that CANNOT spawn
primary_surface_block: 3       # Surface layer block (default: grass = 3) **OVERRIDES CLIMATE**
primary_stone_block: 1         # Underground block (default: stone = 1) **OVERRIDES CLIMATE**
primary_log_block: 6           # Tree trunk block (-1 = use default) **OVERRIDES CLIMATE**
primary_leave_block: 7         # Tree foliage block (-1 = use default) **OVERRIDES CLIMATE**
```

**IMPORTANT - Climate Override:**
The four `primary_*` block properties act as **ultimate calls** that override block climate restrictions:
- Blocks specified here ALWAYS spawn in this biome, even if they lack climate properties
- Blocks specified here ALWAYS spawn even if their climate range doesn't match the biome
- This allows you to use player-only blocks (no climate properties) as biome terrain
- Example: A crafted "Frozen Stone" block with no climate properties can still be `primary_stone_block` in an ice biome

**Usage Example:**
- Desert biome: `blacklisted_blocks: "3"` (no grass blocks)
- Mushroom biome: `required_blocks: "8,9"` (must have mushroom blocks)
- Ice biome: `primary_surface_block: 10` (ice spawns regardless of ice block's climate properties)

### Structure Control

```yaml
required_structures: "village,well"      # Structures that must spawn
blacklisted_structures: "desert_temple"  # Structures that cannot spawn
```

### Creature Control

```yaml
blacklisted_creatures: "zombie,skeleton" # Creatures that cannot spawn
hostile_spawn: true                      # Allow hostile mobs
```

### Weather and Atmosphere

```yaml
primary_weather: "rain"                  # Primary weather type
blacklisted_weather: "snow,hail"         # Weather types that cannot occur
fog_color: "135,179,230"                 # RGB fog color (0-255)
```

### Ore Distribution

```yaml
ore_spawn_rates: "coal:1.5,iron:2.0,gold:0.5"
# Format: "ore_name:multiplier,..."
# 1.0 = normal, 2.0 = double, 0.5 = half
```

### Complete Example

```yaml
name: "snowy_mountains"
temperature: 10
moisture: 60
age: 20
activity: 30

spawn_location: "AboveGround"
lowest_y: 64
underwater_biome: false
river_compatible: true
biome_rarity_weight: 25
height_multiplier: 2.5

trees_spawn: true
tree_density: 20
vegetation_density: 10

required_blocks: "11,12"        # Snow, ice blocks
blacklisted_blocks: "3,4"       # No grass or sand
primary_surface_block: 11       # Snow
primary_stone_block: 1          # Stone
primary_log_block: 13           # Spruce log
primary_leave_block: 14         # Spruce leaves

blacklisted_structures: "desert_temple,cactus"
blacklisted_creatures: "desert_zombie"
hostile_spawn: true

primary_weather: "snow"
blacklisted_weather: "rain"
fog_color: "200,220,255"

ore_spawn_rates: "coal:1.2,emerald:2.0"
```

## Structure Definition (.yaml)

```yaml
name: "structure_name"
size: [width, height, depth]  # In blocks
origin: [x, y, z]             # Anchor point

blocks:
  - [x, y, z, "block_type"]   # Block positions
  - [x, y, z, "block_type"]
  # ... more blocks
```

---

# Appendix D: Third-Party Licenses

See `THIRD-PARTY-LICENSES.md` for complete license information.

**Key Dependencies:**
- **Vulkan** - Khronos Group
- **GLFW 3.4** - zlib/libpng license
- **Dear ImGui 1.91.9b** - MIT License
- **GLM** - MIT License
- **yaml-cpp** - MIT License
- **FastNoiseLite** - MIT License

---

# Appendix E: Glossary

**Biome** - Region of world with specific terrain characteristics

**Chunk** - 32×32×32 voxel volume, basic unit of world storage

**ConVar** - Console Variable, configurable setting

**Cube Map** - 6-sided texture for skybox or block faces

**Greedy Meshing** - Optimization combining adjacent faces

**Mesh** - Collection of vertices forming renderable geometry

**Noclip** - Debug mode allowing flying through walls

**RLE** - Run-Length Encoding, compression algorithm

**SPIR-V** - Compiled shader format for Vulkan

**Voxel** - 3D pixel, basic unit of world

**World Streaming** - Dynamically loading/unloading chunks

---

**End of Handbook**

For latest updates and detailed documentation, see the `docs/` directory.

**Version:** 2.1
**Last Updated:** 2025-11-26
**Maintained by:** Voxel Engine Team
