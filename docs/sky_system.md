# Sky System Guide

The voxel engine features a **dual cube map sky system** with natural blue sky and star-filled night sky, combined with procedural sun/moon effects for dynamic day/night cycles.

## Overview

The sky system provides:
- ☀️ **Minecraft-Style Day/Night Cycle** - 24000 ticks (20 minutes real time)
- 🟦 **Square Sun & Moon** - Voxel aesthetic with dreamy gradients
- ⭐ **Baked Star Field** - Texture-based stars with real-time twinkling (red, blue, white)
- 🌫️ **Dynamic Fog** - Fog color matches time of day
- 💡 **Ambient Lighting** - World brightness changes with sun/moon
- 🎨 **Dreamy Dawn/Dusk** - Orange, pink, and purple gradient transitions
- 🌌 **Dual Cube Maps** - Separate day (blue sky) and night (stars) textures

## Time System

### Time of Day (0-1 Scale)

| Value | Time | Description |
|-------|------|-------------|
| `0.0` | Midnight | 🌙 Dark night, stars visible, moon at zenith |
| `0.25` | Sunrise | 🌅 Orange/pink sky, sun rising |
| `0.5` | Noon | ☀️ Bright day, sun overhead |
| `0.75` | Sunset | 🌆 Golden hour, sun setting |
| `1.0` | Midnight | 🌙 Wraps back to 0.0 |

### Time Progression

At **timespeed = 1.0** (normal speed):
- Full day/night cycle = **20 minutes** of real time (Minecraft-compatible)
- **24000 ticks** = 1 full cycle (20 ticks/second)
- Day time starts at **0.25** (sunrise/morning)
- Day is longer than night for better gameplay
- Time flows automatically by default

## Console Commands

### `skytime <0-1>`

Set the time of day instantly.

**Usage:**
```
skytime 0.0      # Set to midnight
skytime 0.25     # Set to sunrise
skytime 0.5      # Set to noon
skytime 0.75     # Set to sunset
skytime          # Show current time
```

**Examples:**
```
skytime 0.3      # Mid-morning
skytime 0.7      # Late afternoon
skytime 0.15     # Early dawn
```

**Notes:**
- Values automatically clamped to 0-1 range
- Shows helpful time descriptions (night/sunrise/day/sunset)
- Instantly changes sky, lighting, and fog

### `timespeed <value>`

Control how fast time progresses.

**Usage:**
```
timespeed 0      # Pause time
timespeed 1      # Normal speed (20 min cycle) - default
timespeed 10     # 10x faster (2 min cycle)
timespeed 0.1    # 10% speed (3.3 hour cycle)
timespeed        # Show current speed
```

**Common Speeds:**

| Speed | Cycle Duration | Use Case |
|-------|---------------|----------|
| `0` | ⏸️ Paused | Lock time at specific moment |
| `0.1` | 240 minutes | Slow, realistic |
| `1` | 24 minutes | Normal gameplay |
| `10` | 2.4 minutes | Fast timelapse |
| `100` | 14.4 seconds | Extreme timelapse |

**Tab Completion:**
Press Tab after `timespeed ` to cycle through: `0`, `0.1`, `1`, `10`, `100`

## Visual Effects

### Sun

- **Shape:** Square (voxel aesthetic)
- **Size:** 0.025 units (half-width)
- **Core:** Bright yellow-white center
- **Gradient:** Dreamy purple-to-orange glow
- **Movement:** Travels across sky based on time
- **Intensity:** Peaks at noon (0.5)

### Moon

- **Shape:** Square (voxel aesthetic)
- **Size:** 0.020 units (slightly smaller than sun)
- **Color:** Cool blue-white tint
- **Movement:** Independent path, 1.75x faster than sun
- **Speed:** Faster movement compensates for shorter night
- **Visibility:** Strongest at midnight (0.0 / 1.0)

### Stars

- **Type:** Baked into night cube map texture
- **Density:** 0.75% of pixels (192 stars per 256x256 face)
- **Colors:** Red (15%), blue (15%), white (70%)
- **Brightness:** Variable (70-100% per star)
- **Twinkling:** Real-time shader effect (0-100% brightness range)
- **Distribution:** Even across all cube faces (except bottom)
- **Performance:** Zero cost (pre-rendered into texture)

### Fog & Lighting

**Day (0.3 - 0.7):**
- Fog: Light blue (0.7, 0.85, 1.0)
- Brightness: 100% (0.3 base + 0.7 from sun)

**Dawn/Dusk (0.2-0.3 and 0.7-0.8):**
- Fog: Orange/pink (1.0, 0.7, 0.5)
- Brightness: 60-80%

**Night (0.0-0.2 and 0.8-1.0):**
- Fog: Nearly black (0.02, 0.02, 0.02)
- Brightness: 45% (0.3 base + 0.15 from moon)

## Usage Examples

### Lock Time at Golden Hour
```
skytime 0.75
timespeed 0
```
Perfect for screenshots and showcasing builds.

### Fast Day/Night Preview
```
skytime 0.4
timespeed 10
```
Watch the sun set quickly!

### Slow Cinematic Day
```
skytime 0.25
timespeed 0.5
```
Slow sunrise for cinematic experiences.

### Normal Gameplay
```
skytime 0.5
timespeed 1
```
Start at noon with natural day/night cycle.

### Debug Night Effects
```
skytime 0.0
timespeed 0
```
Test stars and moon rendering.

## Technical Details

### Architecture

**Rendering:**
- **Dual cube maps:** Day (blue sky) + Night (black with stars)
- Resolution: 256x256 per face (6 faces each)
- Day cube map: Natural blue gradient (zenith to horizon)
- Night cube map: Nearly black with 0.75% baked star pixels
- Memory: ~3MB total (1.5MB per cube map)
- Shaders: `skybox.vert`, `skybox.frag`

**Descriptor Bindings:**
- Binding 0: Uniform buffer (MVP + camera + sky time)
- Binding 1: Block texture atlas
- Binding 2: Day skybox cube map sampler
- Binding 3: Night skybox cube map sampler

**Performance:**
- Skybox renders once per frame
- Depth writes disabled
- Rendered at far plane (depth = 1.0)
- Minimal performance impact

### Time Update Loop

```cpp
// Called every frame in main game loop
ConsoleCommands::updateSkyTime(deltaTime);

// Calculation (Minecraft-compatible):
// time += (deltaTime * timeSpeed) / 1200.0
// where 1200 = 20 minutes * 60 seconds
// Matches Minecraft: 24000 ticks at 20 ticks/second = 1200 seconds
```

### Sky Time Data (Shader Uniform)

```glsl
layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 cameraPos;    // .xyz = position, .w = render distance
    vec4 skyTimeData;  // .x = time, .y = sun, .z = moon, .w = stars
} ubo;
```

**skyTimeData Components:**
- `.x` = Current time (0-1)
- `.y` = Sun intensity (0-1)
- `.z` = Moon intensity (0-1)
- `.w` = Star intensity (0-1)

Intensities are calculated using smooth step functions for natural transitions.

## Customization

### Changing Cycle Duration

Edit `src/console_commands.cpp`, `updateSkyTime()`:
```cpp
// Default: 1200 seconds (20 minutes, Minecraft-compatible)
s_currentSkyTime += (deltaTime * s_timeSpeed) / 1200.0f;

// For 10 minute cycle:
s_currentSkyTime += (deltaTime * s_timeSpeed) / 600.0f;

// For 1 hour cycle:
s_currentSkyTime += (deltaTime * s_timeSpeed) / 3600.0f;
```

### Changing Sky Colors

Edit `src/vulkan_renderer.cpp`, `createProceduralCubeMap()`:
```cpp
// Day sky colors (line ~1784)
glm::vec3 zenithColor = glm::vec3(0.25f, 0.5f, 0.85f);    // Deep blue
glm::vec3 horizonColor = glm::vec3(0.65f, 0.8f, 0.95f);   // Light blue
```

Edit `src/vulkan_renderer.cpp`, `createNightCubeMap()`:
```cpp
// Night sky colors (line ~1914)
glm::vec3 zenithColor = glm::vec3(0.01f, 0.01f, 0.01f);   // Nearly black
glm::vec3 horizonColor = glm::vec3(0.03f, 0.03f, 0.03f);  // Slightly lighter
```

Edit `shaders/shader.frag` for fog colors:
```glsl
vec3 dayFogColor = vec3(0.7, 0.85, 1.0);       // Light blue
vec3 dawnDuskFogColor = vec3(1.0, 0.7, 0.5);   // Orange/pink
vec3 nightFogColor = vec3(0.02, 0.02, 0.02);   // Nearly black
```

### Changing Sun/Moon Properties

Edit `shaders/skybox.frag`:
```glsl
// Sun size and appearance (line ~112)
float sunSize = 0.025;  // Half-width of square

// Moon size and appearance (line ~148)
float moonSize = 0.020;  // Half-width of square

// Moon speed (line ~99)
float moonAngle = moonTime * 3.14159 * 3.5;  // 1.75x faster than sun
```

### Changing Star Density

Edit `src/vulkan_renderer.cpp`, `createNightCubeMap()`:
```cpp
// Star probability (line ~1959)
if (randVal < 0.0075f) {  // 0.75% density
    // Change to 0.015f for 1.5% (twice as many)
    // Change to 0.0037f for 0.37% (half as many)
}
```

## Troubleshooting

**Sky is completely black:**
- Check shaders compiled: `cd shaders && ./compile.sh`
- Verify skybox_vert.spv and skybox_frag.spv exist
- Run `build.bat` to recompile everything

**Time not progressing:**
- Check timespeed: run `timespeed` in console
- Default is 0 (paused), set to 1: `timespeed 1`

**No sun/moon visible:**
- Sun only visible during day (time 0.2-0.8)
- Moon only visible at night (time < 0.2 or > 0.8)
- Try: `skytime 0.5` for noon or `skytime 0.0` for midnight

**Stars not appearing:**
- Stars are baked into night cube map texture
- Only visible when night sky is rendered (sun down)
- Set to night: `skytime 0.0`
- Pause time: `timespeed 0` (freeze at night)
- Stars twinkle - may fade to black periodically
- Look at upper hemisphere (not below horizon)

**Fog color doesn't match sky:**
- Both should update together automatically
- Try setting time again: `skytime 0.5`
- If persistent, restart engine

## See Also

- [docs/console.md](console.md) - Console usage guide
- [docs/controls.md](controls.md) - Game controls
- [docs/commands.md](commands.md) - Adding custom commands
- [docs/progress.md](progress.md) - Implementation details
