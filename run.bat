@echo off
REM ============================================================================
REM Windows Run Script for Voxel Engine
REM ============================================================================

if not exist "build\Release\voxel-engine.exe" (
    echo Executable not found!
    echo.
    echo Please build the project first by running: build.bat
    echo.
    pause
    exit /b 1
)

echo Starting Voxel Engine...
echo.
cd build\Release
voxel-engine.exe
cd ..\..
