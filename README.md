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

🌍 **Procedural World Generation**
- Infinite terrain using FastNoise
- Configurable world size and seed
- Chunk-based rendering system

🎮 **First-Person Controls**
- WASD movement
- Mouse look camera
- Space/Shift for vertical movement
- ESC for pause menu

🎨 **User Interface**
- ImGui integration
- Crosshair overlay
- Pause menu with Resume/Quit options

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

For detailed build instructions, see [BUILD_INSTRUCTIONS.md](BUILD_INSTRUCTIONS.md)

## Controls

| Key | Action |
|-----|--------|
| **W/A/S/D** | Move forward/left/backward/right |
| **Mouse** | Look around |
| **Space** | Move up |
| **Shift** | Move down |
| **ESC** | Pause menu |

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
│   └── player.cpp
├── include/            # Headers
├── shaders/            # GLSL shaders
│   ├── shader.vert
│   └── shader.frag
├── assets/             # Game assets
│   └── blocks/         # Block definitions
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

- [BUILD_INSTRUCTIONS.md](BUILD_INSTRUCTIONS.md) - Comprehensive build guide
- [QUICK_START_WINDOWS.md](QUICK_START_WINDOWS.md) - Windows quick setup
- [WINDOWS_SETUP.md](WINDOWS_SETUP.md) - Detailed Windows configuration

## Development Status

🚀 **Recently Completed:**
- ✅ Migrated from OpenGL to Vulkan
- ✅ Fixed coordinate system (Y-axis flip)
- ✅ Implemented proper face culling
- ✅ Integrated ImGui with Vulkan backend
- ✅ Added pause menu and crosshair
- ✅ All camera controls working correctly

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
