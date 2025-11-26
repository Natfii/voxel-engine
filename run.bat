@echo off
REM ============================================================================
REM Windows Run Script for Voxel Engine
REM ============================================================================
REM Usage:
REM   run.bat          - Normal launch (shows main menu)
REM   run.bat -debug   - Debug mode (skip menu, small world, fast iteration)
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
if "%1"=="-debug" echo [DEBUG MODE - Small world, no menu]
if "%1"=="--debug" echo [DEBUG MODE - Small world, no menu]
echo.
cd build\Release
voxel-engine.exe %*
cd ..\..
