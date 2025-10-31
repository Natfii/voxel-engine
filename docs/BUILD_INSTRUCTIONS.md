# Build Instructions

This document explains how to build and run the Voxel Engine using the automated build scripts.

## Quick Start

### Windows

1. **Install Vulkan SDK** (if not already installed):
   - Download from https://vulkan.lunarg.com/sdk/home#windows
   - Make sure to check "Shader Toolchain" during installation
   - **Restart your computer** after installation

2. **Build the project:**
   ```cmd
   build.bat
   ```

3. **Run the game:**
   ```cmd
   run.bat
   ```

### Linux

1. **Install dependencies:**
   ```bash
   # Ubuntu/Debian
   sudo apt install vulkan-tools libvulkan-dev vulkan-validationlayers-dev \
                    libglfw3-dev glslang-tools

   # Arch Linux
   sudo pacman -S vulkan-devel glfw shaderc
   ```

2. **Build the project:**
   ```bash
   ./build.sh
   ```

3. **Run the game:**
   ```bash
   ./run.sh
   ```

## What the Build Scripts Do

Both `build.bat` (Windows) and `build.sh` (Linux) perform the following steps automatically:

1. **Check for Vulkan SDK/libraries** - Verifies that Vulkan is installed
2. **Check shaders directory** - Creates `shaders/` folder if it doesn't exist
3. **Compile shaders** - Compiles GLSL shaders to SPIR-V format
4. **Setup build directory** - Creates `build/` folder if needed
5. **Run CMake** - Configures the project
6. **Build the project** - Compiles all source files
7. **Copy assets** - Copies assets, config, shaders, and docs to build directory
8. **Report success** - Shows where the executable is located

## Build Script Features

### Windows (`build.bat`)

- ✅ Automatic Vulkan SDK detection
- ✅ Detects Visual Studio 2022 or 2019 automatically
- ✅ Tries both `glslc` and `glslangValidator` for shader compilation
- ✅ Clear error messages with solutions
- ✅ Builds in Release mode by default
- ✅ Creates `build/Release/voxel-engine.exe`

### Linux (`build.sh`)

- ✅ Checks for system Vulkan or Vulkan SDK
- ✅ Verifies all required libraries (glfw3, etc.)
- ✅ Colored output for easy reading
- ✅ Automatic shader compiler detection
- ✅ Uses all CPU cores for faster compilation (`make -j`)
- ✅ Creates `build/voxel-engine`

## Troubleshooting

### Windows

**"VULKAN_SDK environment variable not found"**
- Install Vulkan SDK from https://vulkan.lunarg.com/
- Restart your computer (important!)
- Verify by opening Command Prompt and typing: `echo %VULKAN_SDK%`

**"Visual Studio not detected"**
- Install Visual Studio 2019 or 2022 with "Desktop development with C++" workload
- Or install Visual Studio Build Tools

**"No shader compiler found"**
- Reinstall Vulkan SDK and ensure "Shader Toolchain" is checked

### Linux

**"Vulkan not found"**
```bash
# Ubuntu/Debian
sudo apt install vulkan-tools libvulkan-dev vulkan-validationlayers-dev

# Arch Linux
sudo pacman -S vulkan-devel vulkan-tools
```

**"Missing required libraries: glfw3"**
```bash
# Ubuntu/Debian
sudo apt install libglfw3-dev

# Arch Linux
sudo pacman -S glfw
```

**"No shader compiler found"**
```bash
# Ubuntu/Debian
sudo apt install glslang-tools

# Arch Linux
sudo pacman -S shaderc
```

**Permission denied when running scripts**
```bash
chmod +x build.sh run.sh
```

## Manual Build (Advanced)

If you prefer to build manually or need to customize the build:

### Windows (Manual)
```cmd
REM 1. Compile shaders
cd shaders
"%VULKAN_SDK%\Bin\glslc.exe" shader.vert -o vert.spv
"%VULKAN_SDK%\Bin\glslc.exe" shader.frag -o frag.spv
cd ..

REM 2. CMake configuration
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64

REM 3. Build
cmake --build . --config Release

REM 4. Run
cd Release
voxel-engine.exe
```

### Linux (Manual)
```bash
# 1. Compile shaders
cd shaders
glslc shader.vert -o vert.spv
glslc shader.frag -o frag.spv
cd ..

# 2. CMake configuration
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release

# 3. Build
make -j$(nproc)

# 4. Run
./voxel-engine
```

## Build Options

### Debug Build (Linux)
```bash
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

### Debug Build (Windows)
```cmd
cd build
cmake --build . --config Debug
```

Debug builds enable:
- Vulkan validation layers
- More detailed error messages
- Better debugging information

## Clean Build

To start fresh:

### Windows
```cmd
rmdir /s /q build
build.bat
```

### Linux
```bash
rm -rf build
./build.sh
```

## File Structure After Build

```
voxel-engine/
├── build/              # Build output directory
│   ├── Release/        # (Windows) Release executables
│   │   └── voxel-engine.exe
│   ├── voxel-engine    # (Linux) Executable
│   ├── assets/         # Copied assets (blocks, etc.)
│   ├── config.ini      # Copied configuration
│   ├── docs/           # Copied documentation
│   │   ├── console.md
│   │   ├── controls.md
│   │   └── ...
│   └── shaders/        # Copied compiled shaders
│       ├── vert.spv
│       └── frag.spv
├── shaders/            # Source shaders
│   ├── shader.vert
│   ├── shader.frag
│   ├── vert.spv        # Compiled vertex shader
│   └── frag.spv        # Compiled fragment shader
├── docs/               # Source documentation
│   ├── BUILD_INSTRUCTIONS.md
│   ├── console.md
│   └── ...
└── ...
```

## Performance Tips

### Windows
- Build in **Release** mode for best performance (default in `build.bat`)
- Debug builds are ~3-5x slower due to validation layers

### Linux
- Use **Release** build type: `-DCMAKE_BUILD_TYPE=Release`
- Compile with all cores: `make -j$(nproc)`

## Getting Help

If you encounter issues:
1. Check `docs/WINDOWS_SETUP.md` for Windows-specific setup
2. Check `docs/QUICK_START_WINDOWS.md` for a quick Windows guide
3. Make sure your GPU supports Vulkan (use `vulkaninfo`)
4. Update your graphics drivers
5. Check the main `README.md` for troubleshooting tips
6. In-game, press **F9** and type `docs/console.md` for console help

## System Requirements

**Minimum:**
- Windows 10/11 (64-bit) or Linux
- GPU with Vulkan 1.0 support
- 4GB RAM
- Visual Studio 2019+ (Windows) or GCC 7+ (Linux)

**Supported GPUs:**
- NVIDIA GeForce 600 series or newer
- AMD Radeon HD 7000 series or newer
- Intel HD Graphics 4000 or newer

---

