# Voxel Engine

A modern voxel-based game engine built with **Vulkan**, featuring procedural terrain generation and first-person camera controls.

![Vulkan](https://img.shields.io/badge/Vulkan-1.3-red)
![C++](https://img.shields.io/badge/C++-17-blue)
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

- **Windows**: Visual Studio 2019+, Vulkan SDK, CMake 3.10+
- **Linux**: GCC 7+, Vulkan development libraries, GLFW3, CMake 3.10+

### Building

#### Windows
```cmd
build.bat
run.bat
```

#### Linux
```bash
./build.sh
./run.sh
```

For detailed build instructions, see [docs/BUILD_INSTRUCTIONS.md](docs/BUILD_INSTRUCTIONS.md)

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

For more details, see [docs/controls.md](docs/controls.md)

## Configuration

Edit `config.ini` to customize:
- World seed
- World dimensions (width, height, depth)
- Rendering settings

Example:
```ini
[World]
seed = 1124345
world_width = 12
world_height = 3
world_depth = 12
```

## Project Structure

```
voxel-engine/
├── src/                # Source code
│   ├── main.cpp
│   ├── vulkan_renderer.cpp
│   ├── chunk.cpp
│   ├── world.cpp
│   ├── player.cpp
│   ├── console.cpp
│   └── ...
├── include/            # Headers
├── shaders/            # GLSL shaders
│   ├── shader.vert
│   └── shader.frag
├── assets/             # Game assets
│   └── blocks/         # Block definitions
├── docs/               # Documentation
│   ├── console.md
│   ├── controls.md
│   └── ...
├── external/           # Third-party libraries
│   ├── imgui/
│   ├── glfw/
│   └── yaml-cpp/
└── build/              # Build output
```

## Technical Details

**Graphics API**: Vulkan 1.3
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

### Setup & Building
- [docs/BUILD_INSTRUCTIONS.md](docs/BUILD_INSTRUCTIONS.md) - Comprehensive build guide
- [docs/QUICK_START_WINDOWS.md](docs/QUICK_START_WINDOWS.md) - Windows quick setup
- [docs/WINDOWS_SETUP.md](docs/WINDOWS_SETUP.md) - Detailed Windows configuration

### Gameplay & Development
- [docs/controls.md](docs/controls.md) - Game controls reference
- [docs/console.md](docs/console.md) - Developer console usage
- [docs/commands.md](docs/commands.md) - Adding custom console commands
- [docs/sky_system.md](docs/sky_system.md) - Sky system and time control guide
- [docs/CUBE_MAP_GUIDE.md](docs/CUBE_MAP_GUIDE.md) - Cube map textures and block definitions

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
