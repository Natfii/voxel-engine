@echo off
REM ============================================================================
REM Windows Build Script for Voxel Engine (Vulkan)
REM ============================================================================
REM REQUIRED VERSIONS:
REM   - CMake 3.29.0 or higher
REM   - Visual Studio 2022 (17.0+)
REM   - Vulkan SDK 1.4.x
REM ============================================================================
setlocal enabledelayedexpansion

echo.
echo ============================================================================
echo   Voxel Engine - Windows Build Script
echo ============================================================================
echo.

REM Required versions
set REQUIRED_CMAKE_MAJOR=3
set REQUIRED_CMAKE_MINOR=29
set REQUIRED_VS_YEAR=2019
set REQUIRED_VULKAN_MAJOR=1
set REQUIRED_VULKAN_MINOR=4

REM Check for -clean flag
if "%1"=="-clean" goto clean
if "%1"=="--clean" goto clean
if "%1"=="-help" goto help
if "%1"=="--help" goto help
if "%1"=="/?" goto help
goto skip_clean

:help
echo.
echo Usage: build.bat [options]
echo.
echo Options:
echo   -clean, --clean    Remove build directory and recompile
echo   -help, --help, /?  Show this help message
echo.
echo REQUIRED BUILD TOOLS:
echo   - CMake 3.29.0+       https://cmake.org/download/
echo   - Build Tools 2019+   https://aka.ms/vs/16/release/vs_buildtools.exe
echo   - Vulkan SDK 1.4+     https://vulkan.lunarg.com/sdk/home#windows
echo.
echo The build script will verify all tools are installed and meet version requirements.
echo.
pause
exit /b 0

:clean
echo [CLEAN] Removing build directory and compiled shaders...
if exist "build" (
    echo Deleting build directory...
    rmdir /s /q build
)
if exist "shaders\*.spv" (
    echo Deleting compiled shaders...
    del /q shaders\*.spv
)
echo Clean complete!
echo.

:skip_clean

REM ============================================================================
REM VERSION CHECKS
REM ============================================================================

echo ============================================================================
echo   CHECKING BUILD TOOL VERSIONS
echo ============================================================================
echo.

set BUILD_OK=1

REM --------------------------------------------------------------------------
REM Check CMake version
REM --------------------------------------------------------------------------
echo [1/3] Checking CMake version...

where cmake >nul 2>&1
if errorlevel 1 (
    echo.
    echo [ERROR] CMake not found in PATH!
    echo.
    echo   REQUIRED: CMake %REQUIRED_CMAKE_MAJOR%.%REQUIRED_CMAKE_MINOR%.0 or higher
    echo.
    echo   Download from: https://cmake.org/download/
    echo   Make sure to add CMake to PATH during installation.
    echo.
    set BUILD_OK=0
    goto check_vs
)

for /f "tokens=3" %%v in ('cmake --version 2^>^&1 ^| findstr /i "version"') do set CMAKE_VERSION=%%v
for /f "tokens=1,2 delims=." %%a in ("%CMAKE_VERSION%") do (
    set CMAKE_MAJOR=%%a
    set CMAKE_MINOR=%%b
)

echo   Found: CMake %CMAKE_VERSION%
echo   Required: CMake %REQUIRED_CMAKE_MAJOR%.%REQUIRED_CMAKE_MINOR%.0+

if %CMAKE_MAJOR% LSS %REQUIRED_CMAKE_MAJOR% (
    echo.
    echo [ERROR] CMake version too old!
    echo   Your version: %CMAKE_VERSION%
    echo   Required: %REQUIRED_CMAKE_MAJOR%.%REQUIRED_CMAKE_MINOR%.0+
    echo.
    echo   Please update CMake: https://cmake.org/download/
    echo.
    set BUILD_OK=0
) else if %CMAKE_MAJOR% EQU %REQUIRED_CMAKE_MAJOR% (
    if %CMAKE_MINOR% LSS %REQUIRED_CMAKE_MINOR% (
        echo.
        echo [ERROR] CMake version too old!
        echo   Your version: %CMAKE_VERSION%
        echo   Required: %REQUIRED_CMAKE_MAJOR%.%REQUIRED_CMAKE_MINOR%.0+
        echo.
        echo   Please update CMake: https://cmake.org/download/
        echo.
        set BUILD_OK=0
    ) else (
        echo   [OK] CMake version is compatible
    )
) else (
    echo   [OK] CMake version is compatible
)
echo.

:check_vs
REM --------------------------------------------------------------------------
REM Check Visual Studio / Build Tools version
REM --------------------------------------------------------------------------
echo [2/3] Checking Visual Studio / Build Tools version...

set VS_FOUND=0
set VS_PATH=
set VS_YEAR=

REM Check for VS 2022 (preferred)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC" (
    set VS_FOUND=1
    set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community"
    set VS_EDITION=Community
    set VS_YEAR=2022
    goto vs_found
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC" (
    set VS_FOUND=1
    set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Professional"
    set VS_EDITION=Professional
    set VS_YEAR=2022
    goto vs_found
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC" (
    set VS_FOUND=1
    set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Enterprise"
    set VS_EDITION=Enterprise
    set VS_YEAR=2022
    goto vs_found
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC" (
    set VS_FOUND=1
    set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\BuildTools"
    set VS_EDITION=BuildTools
    set VS_YEAR=2022
    goto vs_found
)

REM Check for VS 2019 in Program Files (64-bit install)
if exist "C:\Program Files\Microsoft Visual Studio\2019\Community\VC" (
    set VS_FOUND=1
    set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2019\Community"
    set VS_EDITION=Community
    set VS_YEAR=2019
    goto vs_found
)
if exist "C:\Program Files\Microsoft Visual Studio\2019\Professional\VC" (
    set VS_FOUND=1
    set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2019\Professional"
    set VS_EDITION=Professional
    set VS_YEAR=2019
    goto vs_found
)
if exist "C:\Program Files\Microsoft Visual Studio\2019\Enterprise\VC" (
    set VS_FOUND=1
    set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2019\Enterprise"
    set VS_EDITION=Enterprise
    set VS_YEAR=2019
    goto vs_found
)
if exist "C:\Program Files\Microsoft Visual Studio\2019\BuildTools\VC" (
    set VS_FOUND=1
    set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2019\BuildTools"
    set VS_EDITION=BuildTools
    set VS_YEAR=2019
    goto vs_found
)

REM VS not found
echo.
echo [ERROR] Visual Studio 2019 or 2022 not found!
echo.
echo   REQUIRED: Visual Studio 2019+ or Build Tools 2019+
echo.
echo   This project requires C++20 features from MSVC 16.x+
echo.
echo   Download Build Tools 2019 from:
echo   https://aka.ms/vs/16/release/vs_buildtools.exe
echo.
echo   Or Visual Studio Community from:
echo   https://visualstudio.microsoft.com/vs/
echo.
echo   During installation, select:
echo   - "Desktop development with C++"
echo   - Windows SDK 10.0.x
echo.
set BUILD_OK=0
goto vs_done

:vs_found
echo   Found: Visual Studio %VS_YEAR% %VS_EDITION%
echo   Path: %VS_PATH%
echo   [OK] Visual Studio version is compatible

:vs_done
echo.

REM --------------------------------------------------------------------------
REM Check Vulkan SDK
REM --------------------------------------------------------------------------
echo [3/3] Checking Vulkan SDK...

if "%VULKAN_SDK%"=="" goto vulkan_not_found

echo   Found: %VULKAN_SDK%

REM Check if glslc.exe exists (proves SDK is valid)
if not exist "%VULKAN_SDK%\Bin\glslc.exe" goto vulkan_invalid

echo   [OK] Vulkan SDK is valid
goto vulkan_done

:vulkan_not_found
echo.
echo [ERROR] VULKAN_SDK environment variable not found!
echo.
echo   REQUIRED: Vulkan SDK %REQUIRED_VULKAN_MAJOR%.%REQUIRED_VULKAN_MINOR%.x
echo.
echo   Download from: https://vulkan.lunarg.com/sdk/home#windows
echo.
echo   After installation:
echo   1. Restart your computer (required for environment variables)
echo   2. Run this build script again
echo.
set BUILD_OK=0
goto vulkan_done

:vulkan_invalid
echo.
echo [ERROR] Vulkan SDK appears invalid (glslc.exe not found)
echo   Path: %VULKAN_SDK%
echo.
echo   Please reinstall Vulkan SDK from: https://vulkan.lunarg.com/sdk/home#windows
echo.
set BUILD_OK=0
goto vulkan_done

:vulkan_done
echo.

:version_summary
REM --------------------------------------------------------------------------
REM Version Check Summary
REM --------------------------------------------------------------------------
echo ============================================================================
if %BUILD_OK%==0 (
    echo   BUILD REQUIREMENTS NOT MET
    echo ============================================================================
    echo.
    echo   Please install/update the required tools and try again.
    echo.
    echo   Required versions:
    echo   - CMake %REQUIRED_CMAKE_MAJOR%.%REQUIRED_CMAKE_MINOR%.0+    https://cmake.org/download/
    echo   - Build Tools 2019+   https://aka.ms/vs/16/release/vs_buildtools.exe
    echo   - Vulkan SDK %REQUIRED_VULKAN_MAJOR%.%REQUIRED_VULKAN_MINOR%.x    https://vulkan.lunarg.com/sdk/home#windows
    echo.
    pause
    exit /b 1
) else (
    echo   ALL BUILD REQUIREMENTS MET
    echo ============================================================================
)
echo.

REM ============================================================================
REM BUILD PROCESS
REM ============================================================================

REM Check if shaders folder exists
if not exist "shaders" (
    echo [4/7] Creating shaders directory...
    mkdir shaders
    echo Shaders directory created.
) else (
    echo [4/7] Shaders directory exists.
)
echo.

REM Compile shaders
echo [5/7] Compiling shaders...
cd shaders

if exist "%VULKAN_SDK%\Bin\glslc.exe" (
    echo Using glslc to compile shaders...

    for %%s in (shader.vert shader.frag line.vert line.frag skybox.vert skybox.frag mesh.vert mesh.frag sphere.vert sphere.frag) do (
        echo   Compiling %%s...
        "%VULKAN_SDK%\Bin\glslc.exe" %%s -o %%~ns_%%~xs.spv 2>nul
        if errorlevel 1 (
            REM Try alternate naming
            if "%%~xs"==".vert" "%VULKAN_SDK%\Bin\glslc.exe" %%s -o %%~ns_vert.spv
            if "%%~xs"==".frag" "%VULKAN_SDK%\Bin\glslc.exe" %%s -o %%~ns_frag.spv
        )
    )

    REM Compile with standard naming
    "%VULKAN_SDK%\Bin\glslc.exe" shader.vert -o vert.spv
    "%VULKAN_SDK%\Bin\glslc.exe" shader.frag -o frag.spv
    "%VULKAN_SDK%\Bin\glslc.exe" line.vert -o line_vert.spv
    "%VULKAN_SDK%\Bin\glslc.exe" line.frag -o line_frag.spv
    "%VULKAN_SDK%\Bin\glslc.exe" skybox.vert -o skybox_vert.spv
    "%VULKAN_SDK%\Bin\glslc.exe" skybox.frag -o skybox_frag.spv
    "%VULKAN_SDK%\Bin\glslc.exe" mesh.vert -o mesh_vert.spv
    "%VULKAN_SDK%\Bin\glslc.exe" mesh.frag -o mesh_frag.spv
    "%VULKAN_SDK%\Bin\glslc.exe" sphere.vert -o sphere_vert.spv
    "%VULKAN_SDK%\Bin\glslc.exe" sphere.frag -o sphere_frag.spv

    if errorlevel 1 (
        echo [ERROR] Shader compilation failed!
        cd ..
        pause
        exit /b 1
    )

    echo   Shaders compiled successfully!
) else (
    echo [ERROR] glslc.exe not found in Vulkan SDK!
    echo Please reinstall Vulkan SDK with "Shader Toolchain" selected.
    cd ..
    pause
    exit /b 1
)

cd ..
echo.

REM Create build directory
echo [6/7] Setting up build directory...
if not exist "build" (
    mkdir build
    echo Build directory created.
) else (
    echo Build directory exists.
)
echo.

REM Run CMake
echo [7/7] Running CMake and building project...
cd build

REM Select the correct Visual Studio generator based on detected version
if "%VS_YEAR%"=="2022" (
    echo Configuring with Visual Studio 2022...
    cmake .. -G "Visual Studio 17 2022" -A x64
) else (
    echo Configuring with Visual Studio 2019...
    cmake .. -G "Visual Studio 16 2019" -A x64
)

if errorlevel 1 (
    echo [ERROR] CMake configuration failed!
    cd ..
    pause
    exit /b 1
)

echo.
echo Building project (Release mode)...
cmake --build . --config Release

if errorlevel 1 (
    echo [ERROR] Build failed!
    cd ..
    pause
    exit /b 1
)

cd ..

echo.
echo ============================================================================
echo   BUILD SUCCESSFUL!
echo ============================================================================
echo.
echo   Executable: build\Release\voxel-engine.exe
echo.
echo   To run the game:
echo     cd build\Release
echo     voxel-engine.exe
echo.
echo   Or simply run: run.bat
echo.
pause
