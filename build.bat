@echo off
REM ============================================================================
REM Windows Build Script for Voxel Engine (Vulkan)
REM ============================================================================
echo.
echo ============================================================================
echo   Voxel Engine - Windows Build Script
echo ============================================================================
echo.

REM Check if Vulkan SDK is installed
if not defined VULKAN_SDK (
    echo [ERROR] VULKAN_SDK environment variable not found!
    echo.
    echo Please install the Vulkan SDK from:
    echo https://vulkan.lunarg.com/sdk/home#windows
    echo.
    echo After installation, restart your computer and try again.
    pause
    exit /b 1
)

echo [1/5] Checking Vulkan SDK...
echo Vulkan SDK found at: %VULKAN_SDK%
echo.

REM Check if shaders folder exists
if not exist "shaders" (
    echo [2/5] Creating shaders directory...
    mkdir shaders
    echo Shaders directory created.
) else (
    echo [2/5] Shaders directory exists.
)
echo.

REM Compile shaders
echo [3/5] Compiling shaders...
cd shaders

if exist "%VULKAN_SDK%\Bin\glslc.exe" (
    echo Using glslc to compile shaders...
    "%VULKAN_SDK%\Bin\glslc.exe" shader.vert -o vert.spv
    if errorlevel 1 (
        echo [ERROR] Failed to compile vertex shader!
        cd ..
        pause
        exit /b 1
    )

    "%VULKAN_SDK%\Bin\glslc.exe" shader.frag -o frag.spv
    if errorlevel 1 (
        echo [ERROR] Failed to compile fragment shader!
        cd ..
        pause
        exit /b 1
    )

    echo Shaders compiled successfully!

) else if exist "%VULKAN_SDK%\Bin\glslangValidator.exe" (
    echo Using glslangValidator to compile shaders...
    "%VULKAN_SDK%\Bin\glslangValidator.exe" -V shader.vert -o vert.spv
    if errorlevel 1 (
        echo [ERROR] Failed to compile vertex shader!
        cd ..
        pause
        exit /b 1
    )

    "%VULKAN_SDK%\Bin\glslangValidator.exe" -V shader.frag -o frag.spv
    if errorlevel 1 (
        echo [ERROR] Failed to compile fragment shader!
        cd ..
        pause
        exit /b 1
    )

    echo Shaders compiled successfully!

) else (
    echo [ERROR] No shader compiler found!
    echo Please reinstall Vulkan SDK with "Shader Toolchain" selected.
    cd ..
    pause
    exit /b 1
)

cd ..
echo.

REM Create build directory
echo [4/5] Setting up build directory...
if not exist "build" (
    mkdir build
    echo Build directory created.
) else (
    echo Build directory exists.
)
echo.

REM Run CMake
echo [5/5] Running CMake and building project...
cd build

REM Detect Visual Studio version
echo Detecting Visual Studio...
if exist "C:\Program Files\Microsoft Visual Studio\2022" (
    echo Found Visual Studio 2022
    cmake .. -G "Visual Studio 17 2022" -A x64
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019" (
    echo Found Visual Studio 2019
    cmake .. -G "Visual Studio 16 2019" -A x64
) else (
    echo Visual Studio not detected, using default generator
    cmake ..
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
echo Executable location: build\Release\voxel-engine.exe
echo.
echo To run the game:
echo   cd build\Release
echo   voxel-engine.exe
echo.
echo Or simply run: run.bat
echo.
pause
