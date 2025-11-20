# Windows Setup Guide for Vulkan

## Step 1: Install Vulkan SDK

1. **Download Vulkan SDK:**
   - Go to: https://vulkan.lunarg.com/sdk/home#windows
   - Download the latest Windows installer (e.g., `VulkanSDK-1.3.xxx-Installer.exe`)

2. **Run the Installer:**
   - Run the downloaded `.exe` file
   - **IMPORTANT**: Check these options during installation:
     - ✅ Shader Toolchain (includes glslc/glslangValidator)
     - ✅ Vulkan Runtime
     - ✅ Debug Runtime (for validation layers)
   - Default install location: `C:\VulkanSDK\1.3.xxx`

3. **Verify Installation:**
   - Open a **new** Command Prompt (important - to pick up environment variables)
   - Run:
     ```cmd
     echo %VULKAN_SDK%
     ```
   - Should show: `C:\VulkanSDK\1.3.xxx` (or similar)

## Step 2: Verify Vulkan Support

Check if your GPU supports Vulkan:
```cmd
cd %VULKAN_SDK%\Bin
vulkaninfoSDK.exe
```

If you see GPU information, you're good! If not, update your graphics drivers.

## Step 3: Configure VS Code (if using)

Create or update `.vscode/c_cpp_properties.json`:

```json
{
    "configurations": [
        {
            "name": "Win32",
            "includePath": [
                "${workspaceFolder}/**",
                "${env:VULKAN_SDK}/Include",
                "${workspaceFolder}/external/glfw-3.4/include",
                "${workspaceFolder}/external/imgui-1.91.9b",
                "${workspaceFolder}/external/imgui-1.91.9b/backends",
                "${workspaceFolder}/include"
            ],
            "defines": [
                "_DEBUG",
                "UNICODE",
                "_UNICODE"
            ],
            "windowsSdkVersion": "10.0.22000.0",
            "compilerPath": "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.xx.xxxxx/bin/Hostx64/x64/cl.exe",
            "cStandard": "c17",
            "cppStandard": "c++17",
            "intelliSenseMode": "windows-msvc-x64"
        }
    ],
    "version": 4
}
```

**Note:** Adjust the `compilerPath` to match your Visual Studio installation.

## Step 4: Compile Shaders (Windows)

The project includes a `shaders/compile.bat` script that automatically compiles all shaders.

Run it:
```cmd
cd shaders
compile.bat
```

This will compile all shader files:
- `shader.vert` and `shader.frag` (main rendering)
- `line.vert` and `line.frag` (line rendering)
- `skybox.vert` and `skybox.frag` (skybox rendering)

The script automatically detects and uses either `glslc` or `glslangValidator` from your Vulkan SDK installation.

## Step 5: Build with CMake (Windows)

```cmd
mkdir build
cd build

REM Configure (Visual Studio 2022)
cmake .. -G "Visual Studio 17 2022" -A x64

REM Build
cmake --build . --config Release

REM Or open voxel-engine.sln in Visual Studio
```

## Step 6: Run

```cmd
cd Release
voxel-engine.exe
```

## Common Windows Issues

### Issue 1: "VULKAN_SDK not found"
**Solution:**
- Restart your computer after installing Vulkan SDK
- Or manually set environment variable:
  ```cmd
  setx VULKAN_SDK "C:\VulkanSDK\1.3.xxx"
  ```
  Then restart terminal

### Issue 2: "glslc is not recognized"
**Solution:**
- Add to PATH: `C:\VulkanSDK\1.3.xxx\Bin`
- Or use full path: `"%VULKAN_SDK%\Bin\glslc.exe"`

### Issue 3: "failed to find suitable GPU"
**Solution:**
- Update graphics drivers:
  - **NVIDIA**: https://www.nvidia.com/Download/index.aspx
  - **AMD**: https://www.amd.com/en/support
  - **Intel**: https://www.intel.com/content/www/us/en/support/products/80939/graphics.html

### Issue 4: CMake can't find Vulkan
**Solution:**
- Make sure VULKAN_SDK environment variable is set
- CMake should automatically find it via `find_package(Vulkan REQUIRED)`
- Check CMake output for: `Found Vulkan: C:/VulkanSDK/...`

### Issue 5: Missing DLLs at runtime
**Solution:**
- Copy `vulkan-1.dll` from `C:\VulkanSDK\1.3.xxx\Bin` to your exe directory
- Or ensure `C:\VulkanSDK\1.3.xxx\Bin` is in system PATH

## Quick Start (TL;DR)

1. Download & install Vulkan SDK: https://vulkan.lunarg.com/sdk/home#windows
2. Restart computer (or at least your terminal)
3. Verify: `echo %VULKAN_SDK%` should show path
4. Compile shaders: `cd shaders && compile.bat`
5. Build: `cmake .. -G "Visual Studio 17 2022" && cmake --build .`
6. Run: `.\Release\voxel-engine.exe`

## Graphics Driver Versions (Minimum for Vulkan)

- **NVIDIA**: GeForce 600 series or newer (Driver 367.xx+)
- **AMD**: Radeon HD 7000 series or newer (Driver 16.3+)
- **Intel**: HD Graphics 4000 or newer (Driver 15.40+)

---

**Note:** After installing Vulkan SDK, **close and reopen VS Code** to pick up the new environment variables!
