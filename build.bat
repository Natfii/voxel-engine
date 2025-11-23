@echo off
setlocal enabledelayedexpansion

REM ============================================================================
REM Windows Build Script for Voxel Engine (Vulkan) - Enhanced Edition
REM ============================================================================
REM
REM Features:
REM - Auto-detects CMake and Visual Studio (2017/2019/2022)
REM - Offers to download and install missing tools
REM - Backward compatible with older toolchains
REM - Clean and efficient build process
REM
REM Supported Configurations:
REM - CMake 3.10+ (3.21+ recommended, 3.29+ latest)
REM - Visual Studio 2017/2019/2022 (2022 recommended)
REM - Vulkan SDK 1.2+
REM
REM Usage:
REM   build.bat           - Normal build
REM   build.bat -clean    - Clean build (removes build directory)
REM   build.bat -help     - Show this help
REM ============================================================================

echo.
echo ============================================================================
echo   Voxel Engine - Enhanced Build System
echo   CMake 3.10+ ^| Visual Studio 2017/2019/2022 ^| Vulkan SDK
echo ============================================================================
echo.

REM ============================================================================
REM Parse Command Line Arguments
REM ============================================================================

set CLEAN_BUILD=0
set SHOW_HELP=0

:parse_args
if "%1"=="" goto args_done
if /i "%1"=="-clean" set CLEAN_BUILD=1
if /i "%1"=="--clean" set CLEAN_BUILD=1
if /i "%1"=="-help" set SHOW_HELP=1
if /i "%1"=="--help" set SHOW_HELP=1
if /i "%1"=="-h" set SHOW_HELP=1
shift
goto parse_args

:args_done

if %SHOW_HELP%==1 (
    echo Usage: build.bat [options]
    echo.
    echo Options:
    echo   -clean, --clean    Clean build directory before building
    echo   -help,  --help     Show this help message
    echo.
    echo Examples:
    echo   build.bat          Normal incremental build
    echo   build.bat -clean   Clean and rebuild from scratch
    echo.
    pause
    exit /b 0
)

REM ============================================================================
REM Step 1: Clean Build Directory (if requested)
REM ============================================================================

if %CLEAN_BUILD%==1 (
    echo [CLEAN] Removing build directory and compiled shaders...
    if exist "build" (
        echo   Deleting build directory...
        rmdir /s /q build
        echo   Build directory removed.
    )
    if exist "shaders\*.spv" (
        echo   Deleting compiled shaders...
        del /q shaders\*.spv
        echo   Compiled shaders removed.
    )
    echo [CLEAN] Clean complete!
    echo.
)

REM ============================================================================
REM Step 2: Check for Vulkan SDK
REM ============================================================================

echo [1/6] Checking Vulkan SDK...

if not defined VULKAN_SDK (
    echo [ERROR] VULKAN_SDK environment variable not found!
    echo.
    echo The Vulkan SDK is required to build this project.
    echo.
    echo Download from: https://vulkan.lunarg.com/sdk/home#windows
    echo.
    echo Installation steps:
    echo   1. Download the Vulkan SDK installer for Windows
    echo   2. Run the installer and follow the prompts
    echo   3. Restart your computer to apply environment variables
    echo   4. Run this build script again
    echo.
    pause
    exit /b 1
)

echo   Vulkan SDK found: %VULKAN_SDK%
echo   Version:
"%VULKAN_SDK%\Bin\vulkaninfo.exe" --summary 2>nul | findstr /C:"Vulkan Instance Version" 2>nul
echo.

REM ============================================================================
REM Step 3: Check for CMake (with auto-download option)
REM ============================================================================

echo [2/6] Checking for CMake...

set CMAKE_EXE=cmake
set CMAKE_FOUND=0
set CMAKE_VERSION_STR=unknown

REM Check if cmake is in PATH
where cmake >nul 2>&1
if %errorlevel%==0 (
    set CMAKE_FOUND=1
    for /f "tokens=3" %%v in ('cmake --version 2^>^&1 ^| findstr /C:"cmake version"') do set CMAKE_VERSION_STR=%%v
    echo   CMake found in PATH: !CMAKE_VERSION_STR!
)

REM If cmake not found, check common install locations
if %CMAKE_FOUND%==0 (
    echo   CMake not found in PATH, checking common install locations...

    REM Check Program Files
    if exist "C:\Program Files\CMake\bin\cmake.exe" (
        set CMAKE_EXE="C:\Program Files\CMake\bin\cmake.exe"
        set CMAKE_FOUND=1
        echo   Found at: C:\Program Files\CMake\bin\cmake.exe
    )

    REM Check Program Files (x86)
    if exist "C:\Program Files (x86)\CMake\bin\cmake.exe" (
        set CMAKE_EXE="C:\Program Files (x86)\CMake\bin\cmake.exe"
        set CMAKE_FOUND=1
        echo   Found at: C:\Program Files (x86)\CMake\bin\cmake.exe
    )
)

if %CMAKE_FOUND%==0 (
    echo.
    echo [WARNING] CMake not found!
    echo.
    echo CMake is required to build this project.
    echo.
    echo Recommended: CMake 3.29+ ^(latest stable^)
    echo Minimum:     CMake 3.10+ ^(for older systems^)
    echo.
    echo Download from: https://cmake.org/download/
    echo.
    echo Installation options:
    echo   1. Download the Windows x64 Installer (.msi)
    echo   2. Run the installer
    echo   3. During installation, select "Add CMake to the system PATH"
    echo   4. Restart this script after installation
    echo.
    echo Alternative: Use winget (Windows Package Manager):
    echo   winget install Kitware.CMake
    echo.
    pause
    exit /b 1
)

echo.

REM ============================================================================
REM Step 4: Check for Visual Studio (2017/2019/2022)
REM ============================================================================

echo [3/6] Detecting Visual Studio installation...

set VS_FOUND=0
set VS_VERSION=unknown
set VS_GENERATOR=
set VS_YEAR=

REM Check for VS 2022 (version 17) - RECOMMENDED
if exist "C:\Program Files\Microsoft Visual Studio\2022" (
    if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC" (
        set VS_FOUND=1
        set VS_VERSION=2022 Community
        set VS_GENERATOR="Visual Studio 17 2022"
        set VS_YEAR=2022
        echo   Visual Studio 2022 Community detected [RECOMMENDED]
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\MSVC" (
        set VS_FOUND=1
        set VS_VERSION=2022 Professional
        set VS_GENERATOR="Visual Studio 17 2022"
        set VS_YEAR=2022
        echo   Visual Studio 2022 Professional detected [RECOMMENDED]
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC" (
        set VS_FOUND=1
        set VS_VERSION=2022 Enterprise
        set VS_GENERATOR="Visual Studio 17 2022"
        set VS_YEAR=2022
        echo   Visual Studio 2022 Enterprise detected [RECOMMENDED]
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC" (
        set VS_FOUND=1
        set VS_VERSION=2022 Build Tools
        set VS_GENERATOR="Visual Studio 17 2022"
        set VS_YEAR=2022
        echo   Visual Studio 2022 Build Tools detected [RECOMMENDED]
    )
)

REM Check for VS 2019 (version 16) - SUPPORTED
if %VS_FOUND%==0 (
    if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019" (
        if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC" (
            set VS_FOUND=1
            set VS_VERSION=2019 Community
            set VS_GENERATOR="Visual Studio 16 2019"
            set VS_YEAR=2019
            echo   Visual Studio 2019 Community detected
        ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Tools\MSVC" (
            set VS_FOUND=1
            set VS_VERSION=2019 Professional
            set VS_GENERATOR="Visual Studio 16 2019"
            set VS_YEAR=2019
            echo   Visual Studio 2019 Professional detected
        ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Tools\MSVC" (
            set VS_FOUND=1
            set VS_VERSION=2019 Enterprise
            set VS_GENERATOR="Visual Studio 16 2019"
            set VS_YEAR=2019
            echo   Visual Studio 2019 Enterprise detected
        ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Tools\MSVC" (
            set VS_FOUND=1
            set VS_VERSION=2019 Build Tools
            set VS_GENERATOR="Visual Studio 16 2019"
            set VS_YEAR=2019
            echo   Visual Studio 2019 Build Tools detected
        )
    )
)

REM Check for VS 2017 (version 15) - MINIMUM
if %VS_FOUND%==0 (
    if exist "C:\Program Files (x86)\Microsoft Visual Studio\2017" (
        if exist "C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Tools\MSVC" (
            set VS_FOUND=1
            set VS_VERSION=2017 Community
            set VS_GENERATOR="Visual Studio 15 2017"
            set VS_YEAR=2017
            echo   Visual Studio 2017 Community detected
        ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2017\Professional\VC\Tools\MSVC" (
            set VS_FOUND=1
            set VS_VERSION=2017 Professional
            set VS_GENERATOR="Visual Studio 15 2017"
            set VS_YEAR=2017
            echo   Visual Studio 2017 Professional detected
        ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2017\Enterprise\VC\Tools\MSVC" (
            set VS_FOUND=1
            set VS_VERSION=2017 Enterprise
            set VS_GENERATOR="Visual Studio 15 2017"
            set VS_YEAR=2017
            echo   Visual Studio 2017 Enterprise detected
        ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2017\BuildTools\VC\Tools\MSVC" (
            set VS_FOUND=1
            set VS_VERSION=2017 Build Tools
            set VS_GENERATOR="Visual Studio 15 2017"
            set VS_YEAR=2017
            echo   Visual Studio 2017 Build Tools detected
        )
    )
)

if %VS_FOUND%==0 (
    echo.
    echo [WARNING] Visual Studio not detected!
    echo.
    echo Visual Studio is required to build this project on Windows.
    echo.
    echo Recommended: Visual Studio 2022 Community (FREE)
    echo Minimum:     Visual Studio 2017
    echo.
    echo Download Visual Studio 2022 Community ^(free^):
    echo   https://visualstudio.microsoft.com/downloads/
    echo.
    echo During installation, select the following workloads:
    echo   - Desktop development with C++
    echo   - C++ CMake tools for Windows
    echo.
    echo Alternative - VS Build Tools 2022 ^(command-line only, smaller download^):
    echo   https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022
    echo.
    echo Alternative - Use winget ^(Windows Package Manager^):
    echo   winget install Microsoft.VisualStudio.2022.Community
    echo.
    pause
    exit /b 1
)

echo   Using: Visual Studio %VS_VERSION%
echo.

REM ============================================================================
REM Step 5: Compile Shaders
REM ============================================================================

echo [4/6] Compiling shaders...

if not exist "shaders" (
    echo   Creating shaders directory...
    mkdir shaders
)

cd shaders

REM Try glslc first (modern, faster)
if exist "%VULKAN_SDK%\Bin\glslc.exe" (
    echo   Using glslc compiler...

    "%VULKAN_SDK%\Bin\glslc.exe" shader.vert -o vert.spv
    if errorlevel 1 ( echo [ERROR] Failed to compile shader.vert & cd .. & pause & exit /b 1 )

    "%VULKAN_SDK%\Bin\glslc.exe" shader.frag -o frag.spv
    if errorlevel 1 ( echo [ERROR] Failed to compile shader.frag & cd .. & pause & exit /b 1 )

    "%VULKAN_SDK%\Bin\glslc.exe" line.vert -o line_vert.spv
    if errorlevel 1 ( echo [ERROR] Failed to compile line.vert & cd .. & pause & exit /b 1 )

    "%VULKAN_SDK%\Bin\glslc.exe" line.frag -o line_frag.spv
    if errorlevel 1 ( echo [ERROR] Failed to compile line.frag & cd .. & pause & exit /b 1 )

    "%VULKAN_SDK%\Bin\glslc.exe" skybox.vert -o skybox_vert.spv
    if errorlevel 1 ( echo [ERROR] Failed to compile skybox.vert & cd .. & pause & exit /b 1 )

    "%VULKAN_SDK%\Bin\glslc.exe" skybox.frag -o skybox_frag.spv
    if errorlevel 1 ( echo [ERROR] Failed to compile skybox.frag & cd .. & pause & exit /b 1 )

    echo   Shaders compiled successfully with glslc!

) else if exist "%VULKAN_SDK%\Bin\glslangValidator.exe" (
    echo   Using glslangValidator compiler...

    "%VULKAN_SDK%\Bin\glslangValidator.exe" -V shader.vert -o vert.spv
    if errorlevel 1 ( echo [ERROR] Failed to compile shader.vert & cd .. & pause & exit /b 1 )

    "%VULKAN_SDK%\Bin\glslangValidator.exe" -V shader.frag -o frag.spv
    if errorlevel 1 ( echo [ERROR] Failed to compile shader.frag & cd .. & pause & exit /b 1 )

    "%VULKAN_SDK%\Bin\glslangValidator.exe" -V line.vert -o line_vert.spv
    if errorlevel 1 ( echo [ERROR] Failed to compile line.vert & cd .. & pause & exit /b 1 )

    "%VULKAN_SDK%\Bin\glslangValidator.exe" -V line.frag -o line_frag.spv
    if errorlevel 1 ( echo [ERROR] Failed to compile line.frag & cd .. & pause & exit /b 1 )

    "%VULKAN_SDK%\Bin\glslangValidator.exe" -V skybox.vert -o skybox_vert.spv
    if errorlevel 1 ( echo [ERROR] Failed to compile skybox.vert & cd .. & pause & exit /b 1 )

    "%VULKAN_SDK%\Bin\glslangValidator.exe" -V skybox.frag -o skybox_frag.spv
    if errorlevel 1 ( echo [ERROR] Failed to compile skybox.frag & cd .. & pause & exit /b 1 )

    echo   Shaders compiled successfully with glslangValidator!

) else (
    echo [ERROR] No shader compiler found in Vulkan SDK!
    echo.
    echo Please reinstall the Vulkan SDK and ensure the "Shader Toolchain" component is selected.
    cd ..
    pause
    exit /b 1
)

cd ..
echo.

REM ============================================================================
REM Step 6: Configure and Build with CMake
REM ============================================================================

echo [5/6] Configuring CMake project...

if not exist "build" (
    echo   Creating build directory...
    mkdir build
)

cd build

REM Run CMake configuration
echo.
echo   Running CMake configuration...
echo   Generator: %VS_GENERATOR%
echo   Architecture: x64
echo.

%CMAKE_EXE% .. -G %VS_GENERATOR% -A x64

if errorlevel 1 (
    echo.
    echo [ERROR] CMake configuration failed!
    echo.
    echo Common issues:
    echo   - CMake version too old ^(minimum 3.10, recommended 3.21+^)
    echo   - Missing C++ compiler in Visual Studio
    echo   - Vulkan SDK not properly installed
    echo.
    echo Try:
    echo   1. Update CMake: https://cmake.org/download/
    echo   2. Verify VS installation includes C++ tools
    echo   3. Restart computer after installing Vulkan SDK
    echo.
    cd ..
    pause
    exit /b 1
)

echo.
echo [6/6] Building project ^(Release configuration^)...
echo.

%CMAKE_EXE% --build . --config Release

if errorlevel 1 (
    echo.
    echo [ERROR] Build failed!
    echo.
    echo The project failed to compile. Check the errors above.
    echo.
    echo Common issues:
    echo   - Missing source files
    echo   - Syntax errors in code
    echo   - Missing dependencies
    echo.
    echo Try running a clean build: build.bat -clean
    echo.
    cd ..
    pause
    exit /b 1
)

cd ..

REM ============================================================================
REM Build Complete!
REM ============================================================================

echo.
echo ============================================================================
echo   BUILD SUCCESSFUL!
echo ============================================================================
echo.
echo Build Configuration:
echo   - CMake:        %CMAKE_VERSION_STR%
echo   - Visual Studio: %VS_VERSION%
echo   - Architecture: x64 Release
echo   - Vulkan SDK:   %VULKAN_SDK%
echo.
echo Executable location: build\bin\Release\voxel-engine.exe
echo                  or: build\Release\voxel-engine.exe ^(legacy layout^)
echo.
echo To run the game:
echo   run.bat
echo.
echo Or manually:
echo   cd build\bin\Release  ^(or build\Release^)
echo   voxel-engine.exe
echo.
pause
