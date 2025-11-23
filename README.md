# Voxel Engine

A modern voxel-based game engine built with **Vulkan**, featuring procedural terrain generation and first-person camera controls.

![Vulkan](https://img.shields.io/badge/Vulkan-1.3-red)
![C++](https://img.shields.io/badge/C++-17-blue)
![CMake](https://img.shields.io/badge/CMake-3.10%2B-green)
![VS](https://img.shields.io/badge/Visual%20Studio-2017%2F2019%2F2022-blue)
![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-lightgrey)

## Features

✨ **Vulkan Rendering**
- Modern graphics API with high performance
- Efficient GPU buffer management
- Back-face culling for optimized rendering

🌅 **Dynamic Sky System**
- Natural blue sky cube map with dynamic day/night transitions
- Minecraft-style 24000-tick day/night cycle (20 minutes)
- Procedural square sun and moon (voxel aesthetic)
- Baked star field with twinkling animation (red, blue, white stars)
- Dreamy dawn/dusk gradients (orange, pink, purple)
- Time-based fog and ambient lighting
- Console commands for time control (`skytime`, `timespeed`)

🌍 **Procedural World Generation**
- Infinite terrain using FastNoise
- Configurable world size and seed
- Chunk-based rendering system

🧱 **Advanced Block & Texture System**
- **Cube map textures**: Different textures for each face of a block
- **Texture atlas**: All textures packed into a single GPU texture
- **YAML-based block definitions**: Easy block creation and modification
- **Texture variation**: Random texture offsets for natural appearance
- **Backwards compatible**: Supports both single-texture and multi-face blocks

🎮 **First-Person Controls**
- WASD movement with sprint
- Mouse look camera
- Physics-based player movement
- Noclip mode for flying

🎨 **User Interface**
- ImGui integration
- Dynamic crosshair with targeting system
- Pause menu with Resume/Quit options

🎯 **Targeting System**
- Unified block targeting and outline rendering
- Rich target information (block name, type, position, distance)
- Context-aware input management
- Debug overlay for target details

🖥️ **Developer Console**
- Source engine-style console (F9)
- Command system with Tab autocomplete
- Console variables (ConVars) with persistence
- Debug overlays (FPS, position, target info)
- Markdown documentation viewer

## Quick Start

### Prerequisites

- **Windows**:
  - Visual Studio 2017/2019/2022 (2022 recommended, Community is free)
  - CMake 3.10+ (3.29+ recommended)
  - Vulkan SDK 1.2+
- **Linux**:
  - GCC 7+ or Clang 5+
  - CMake 3.10+
  - Vulkan development libraries
  - GLFW3

**Note**: The Windows build script will detect your installed tools and provide download links if anything is missing!

### Building

#### Windows (Enhanced Build System)

The build script automatically detects your toolchain and provides helpful guidance:

```cmd
# Normal build (detects CMake, VS 2017/2019/2022, Vulkan SDK)
build.bat

# Clean build (removes old build directory)
build.bat -clean

# Show help
build.bat -help

# Run the game
run.bat
```

**Features:**
- ✅ Auto-detects Visual Studio 2017/2019/2022 (prefers latest)
- ✅ Auto-detects CMake in PATH or common install locations
- ✅ Provides download links if tools are missing
- ✅ Helpful error messages with troubleshooting tips
- ✅ Backward compatible with older toolchains

**Supported Configurations:**
- CMake: 3.10+ (minimum), 3.21+ (recommended), 3.29+ (latest)
- Visual Studio: 2017 (minimum), 2019 (supported), 2022 (recommended)

#### Linux
```bash
./build.sh
./run.sh
```

For detailed build instructions, see [docs/BUILD_INSTRUCTIONS.md](docs/BUILD_INSTRUCTIONS.md) or the [Engine Handbook](ENGINE_HANDBOOK.md#2-quick-start-guide)

## Controls

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

For more details, see the [Engine Handbook - Controls](ENGINE_HANDBOOK.md#basic-controls)

## Configuration

Edit `config.ini` to customize:
- World seed (for procedural generation)
- Control settings (keybindings, mouse sensitivity)
- Window settings (resolution)

Example:
```ini
[World]
seed = 1124

[Window]
width = 800
height = 600
```

Note: World dimensions are handled dynamically through the infinite world streaming system.

## Technical Details

**Graphics API**: Vulkan 1.0

**Math Library**: GLM

**Window/Input**: GLFW 3.4

**UI**: Dear ImGui 1.91.9b

**Noise Generation**: FastNoiseLite

**Configuration**: yaml-cpp

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

## Documentation

### Main Documentation
- **[Engine Handbook](ENGINE_HANDBOOK.md)** - Complete reference covering all systems, architecture, and development guides
- [docs/BUILD_INSTRUCTIONS.md](docs/BUILD_INSTRUCTIONS.md) - Platform-specific build instructions

### Quick Links to Handbook Sections
- [Quick Start Guide](ENGINE_HANDBOOK.md#2-quick-start-guide) - Installation and first run
- [Core Systems](ENGINE_HANDBOOK.md#3-core-systems) - World generation, rendering, lighting, water
- [Architecture & Design](ENGINE_HANDBOOK.md#4-architecture--design) - Threading, memory management, chunk lifecycle
- [Development Guide](ENGINE_HANDBOOK.md#5-development-guide) - Adding features, shaders, testing
- [API Reference](ENGINE_HANDBOOK.md#6-api-reference) - Core classes and utility functions

## Development Status

🚀 **Recently Completed:**
- ✅ Migrated from OpenGL to Vulkan
- ✅ Fixed coordinate system (Y-axis flip)
- ✅ Implemented proper face culling
- ✅ Integrated ImGui with Vulkan backend
- ✅ Added pause menu and crosshair
- ✅ Implemented developer console system
- ✅ Added ConVar system for settings
- ✅ Physics-based player movement
- ✅ Block breaking and texture system
- ✅ Unified targeting system with rich block info
- ✅ Input manager for context-aware controls
- ✅ Tab completion for console commands
- ✅ Dual cube map sky system (day: natural blue, night: black with stars)
- ✅ Minecraft-compatible day/night cycle (24000 ticks = 20 minutes)
- ✅ Procedural square sun and moon with dreamy dawn/dusk effects
- ✅ Baked star textures with real-time twinkling shader
- ✅ Time control console commands (`skytime`, `timespeed`)
- ✅ GPU upload batching (10-15x reduction in GPU sync points)
- ✅ Chunk persistence (save/load to disk)
- ✅ Greedy meshing optimization (50-80% vertex reduction)
- ✅ Mesh buffer pooling (40-60% speedup)
- ✅ Thread-safe world access with proper locking
- ✅ World streaming system with priority-based chunk loading
- ✅ Biome system with multiple terrain types
- ✅ Tree generation and structures
- ✅ Water simulation system
- ✅ Dynamic lighting system
- ✅ Chunk compression and memory optimization
- ✅ Auto-save system with periodic saves
- ✅ World loading/selection UI

## Troubleshooting

### Common Issues

**Black screen or crash on startup:**
- Make sure shaders are compiled (see build instructions)
- Update graphics drivers
- Verify GPU supports Vulkan with `vulkaninfo`

**Controls feel wrong:**
- Check if mouse is captured (click on window)
- Press ESC twice to ensure game is not paused

**Build errors:**
- Ensure Vulkan SDK is installed
- Restart computer after Vulkan SDK installation (Windows)
- Check all dependencies are installed (Linux)

For more troubleshooting, see the build instructions for your platform.

## License

This project is provided as-is for educational purposes.

## Contributing

This is currently a learning/experimental project. Feel free to fork and experiment!

---

**Need Help?** Check the documentation in the repository or open an issue.
