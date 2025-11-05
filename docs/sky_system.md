# Sky System Guide

The voxel engine features a **hybrid sky system** combining cube map skybox rendering with procedural effects for dynamic day/night cycles.

## Overview

The sky system provides:
- ☀️ **Dynamic Day/Night Cycle** - Automatic time progression with configurable speed
- 🌅 **Procedural Sun & Moon** - Realistic sun disc with corona and moon
- ⭐ **Star Field** - Twinkling stars visible at night
- 🌫️ **Dynamic Fog** - Fog color matches time of day
- 💡 **Ambient Lighting** - World brightness changes with sun/moon
- 🎨 **Smooth Transitions** - Beautiful dawn/dusk color gradients

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
- Full day/night cycle = **24 minutes** of real time
- Each in-game hour = 1 minute of real time
- Sunrise to sunset = ~12 minutes
- Night duration = ~12 minutes

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
timespeed 0      # Pause time (default)
timespeed 1      # Normal speed (24 min cycle)
timespeed 10     # 10x faster (2.4 min cycle)
timespeed 0.1    # 10% speed (4 hour cycle)
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

- **Size:** Small, bright disc
- **Color:** Warm yellow-white (255, 255, 230)
- **Corona:** Soft glow around sun
- **Movement:** Rises in east, sets in west
- **Intensity:** Peaks at noon (0.5)

### Moon

- **Size:** Small disc (slightly larger than sun)
- **Color:** Cool blue-white (200, 200, 255)
- **Position:** Always opposite to sun
- **Visibility:** Strongest at midnight (0.0 / 1.0)

### Stars

- **Type:** Procedural point field
- **Distribution:** Random, hash-based
- **Behavior:** Subtle twinkling effect
- **Visibility:** Only visible at night (sun intensity < 0.1)
- **Location:** Upper hemisphere only

### Fog & Lighting

**Day (0.3 - 0.7):**
- Fog: Light blue (0.7, 0.85, 1.0)
- Brightness: 100% (0.3 base + 0.7 from sun)

**Dawn/Dusk (0.2-0.3 and 0.7-0.8):**
- Fog: Orange/pink (1.0, 0.7, 0.5)
- Brightness: 60-80%

**Night (0.0-0.2 and 0.8-1.0):**
- Fog: Dark blue (0.15, 0.2, 0.35)
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
- Cube map: 256x256 per face (6 faces total)
- Procedural gradient: Blue zenith → Lighter horizon
- Memory: ~1.5MB for cube map texture
- Shaders: `skybox.vert`, `skybox.frag`

**Descriptor Bindings:**
- Binding 0: Uniform buffer (MVP + camera + sky time)
- Binding 1: Block texture atlas
- Binding 2: Skybox cube map sampler

**Performance:**
- Skybox renders once per frame
- Depth writes disabled
- Rendered at far plane (depth = 1.0)
- Minimal performance impact

### Time Update Loop

```cpp
// Called every frame in main game loop
ConsoleCommands::updateSkyTime(deltaTime);

// Calculation:
// time += (deltaTime * timeSpeed) / 1440.0
// where 1440 = 24 minutes * 60 seconds
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

Edit `src/console_commands.cpp`, line ~338:
```cpp
// Default: 1440 seconds (24 minutes)
s_currentSkyTime += (deltaTime * s_timeSpeed) / 1440.0f;

// For 12 minute cycle:
s_currentSkyTime += (deltaTime * s_timeSpeed) / 720.0f;

// For 1 hour cycle:
s_currentSkyTime += (deltaTime * s_timeSpeed) / 3600.0f;
```

### Changing Sky Colors

Edit `src/vulkan_renderer.cpp`, `createProceduralCubeMap()`:
```cpp
// Line ~1608
glm::vec3 zenithColor = glm::vec3(0.4f, 0.6f, 0.9f);   // Blue sky
glm::vec3 horizonColor = glm::vec3(0.7f, 0.85f, 1.0f); // Lighter horizon
```

Edit `shaders/shader.frag` for fog colors:
```glsl
vec3 dayFogColor = vec3(0.7, 0.85, 1.0);       // Light blue
vec3 dawnDuskFogColor = vec3(1.0, 0.7, 0.5);   // Orange/pink
vec3 nightFogColor = vec3(0.15, 0.2, 0.35);    // Dark blue
```

### Changing Sun/Moon Properties

Edit `shaders/skybox.frag`:
```glsl
// Sun (line ~77)
if (sunDot > 0.999 && sunIntensity > 0.01) {  // Sun size
    vec3 sunColor = vec3(1.0, 1.0, 0.9);       // Sun color
    skyColor += sunColor * 3.0;                 // Sun brightness
}

// Moon (line ~88)
if (moonDot > 0.998 && moonIntensity > 0.01) { // Moon size
    vec3 moonColor = vec3(0.8, 0.8, 1.0);      // Moon color
    skyColor += moonColor * 1.5;                // Moon brightness
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
- Stars only visible when sun is down
- Set to night: `skytime 0.0`
- Enable time: `timespeed 0` (freeze at night)
- Stars are subtle - look at upper hemisphere

**Fog color doesn't match sky:**
- Both should update together automatically
- Try setting time again: `skytime 0.5`
- If persistent, restart engine

## See Also

- [docs/console.md](console.md) - Console usage guide
- [docs/controls.md](controls.md) - Game controls
- [docs/commands.md](commands.md) - Adding custom commands
- [docs/progress.md](progress.md) - Implementation details
