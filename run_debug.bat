@echo off
REM ============================================================================
REM Quick Debug Launch for Voxel Engine
REM ============================================================================
REM Launches game in debug mode:
REM   - Skips main menu
REM   - Uses small world (3 chunk radius instead of 6)
REM   - Fixed seed (12345) for reproducibility
REM   - No biome modifiers
REM ============================================================================

call run.bat -debug
