# Windows Quick Start Guide

## ⚠️ Before You Start

You're seeing include errors because **Vulkan SDK is not installed yet**. This is expected!

## Installation Steps (5 minutes)

### 1. Install Vulkan SDK

**Download:** https://vulkan.lunarg.com/sdk/home#windows

- Click "Windows" tab
- Download the latest installer (e.g., `VulkanSDK-1.3.275.0-Installer.exe`)
- Run the installer
- **Make sure these are checked:**
  - ✅ Shader Toolchain Debug
  - ✅ Shader Toolchain Release
  - ✅ Vulkan Runtime Components
- Install to default location: `C:\VulkanSDK\1.3.xxx`

### 2. Restart Your Computer

**Important:** Windows needs to restart to load the new environment variables.

After restart, open Command Prompt and verify:
```cmd
echo %VULKAN_SDK%
```

Should show: `C:\VulkanSDK\1.3.xxx`

### 3. Compile Shaders

```cmd
cd path\to\voxel-engine\shaders
compile.bat
```

You should see:
```
SUCCESS! Shaders compiled:
  - vert.spv
  - frag.spv
```

### 4. Configure VS Code (Optional but Recommended)

Copy the example config:
```cmd
copy .vscode\c_cpp_properties.json.example .vscode\c_cpp_properties.json
```

Edit `.vscode/c_cpp_properties.json` and update:
- `compilerPath`: Path to your MSVC compiler (adjust version)
- `windowsSdkVersion`: Your Windows SDK version

Then **restart VS Code** to pick up changes.

### 5. Build the Project

**Option A: Visual Studio (Recommended)**
```cmd
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
```

This creates `voxel-engine.sln` - open it in Visual Studio and press F5 to build & run.

**Option B: Command Line**
```cmd
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
cd Release
voxel-engine.exe
```

## Troubleshooting

### "VULKAN_SDK not found"
→ You didn't restart your computer after installing Vulkan SDK
→ Restart and try again

### "glslc.exe is not recognized"
→ Open a **new** Command Prompt (must be after restart)
→ Verify: `echo %VULKAN_SDK%` shows a path

### "failed to find suitable GPU"
→ Your GPU needs Vulkan support - update your graphics drivers:
- **NVIDIA**: https://www.nvidia.com/Download/index.aspx
- **AMD**: https://www.amd.com/en/support
- **Intel**: https://www.intel.com/content/www/us/en/download-center/home.html

### VS Code still shows red squiggles
→ Copy `.vscode/c_cpp_properties.json.example` to `.vscode/c_cpp_properties.json`
→ Edit the `compilerPath` to match your Visual Studio installation
→ Restart VS Code (`Ctrl+Shift+P` → "Reload Window")

### CMake can't find Vulkan
→ Check that `VULKAN_SDK` environment variable exists:
```cmd
echo %VULKAN_SDK%
```
→ If not set, manually set it in System Environment Variables and restart

### Build succeeds but crashes on startup
→ Make sure you compiled the shaders (`shaders/compile.bat`)
→ Check that `shaders/vert.spv` and `shaders/frag.spv` exist
→ These files are copied to the build directory automatically by CMake

## Check Your GPU Supports Vulkan

After installing Vulkan SDK:
```cmd
cd %VULKAN_SDK%\Bin
vulkaninfoSDK.exe
```

If you see your GPU info, you're good! If not, update drivers.

## Minimum Requirements

- **Windows 10/11** (64-bit)
- **GPU**:
  - NVIDIA GeForce 600 series or newer
  - AMD Radeon HD 7000 series or newer
  - Intel HD Graphics 4000 or newer
- **Visual Studio 2019 or 2022** (for C++ build tools)
- **CMake 3.10+**

## Success Looks Like This

After building, you should be able to run:
```cmd
cd build\Release
voxel-engine.exe
```

And see a window with your voxel terrain rendered using Vulkan! 🎉

## Using the Game

Once running, you can:

**Movement:**
- **W/A/S/D** - Move around
- **Space** - Jump
- **Shift** - Sprint
- **Mouse** - Look around

**Developer Console (F9):**
- Press **F9** to open the console
- Type `help` to see all commands
- Try `noclip` to fly around
- Type `debug drawfps` for FPS counter
- Type `docs/console.md` to read console documentation

**Other Controls:**
- **N** - Toggle noclip mode
- **ESC** - Pause menu
- **Left Click** - Break blocks

---

**Still having issues?** Check the full guide: `WINDOWS_SETUP.md`
