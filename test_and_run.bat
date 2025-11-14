@echo off
REM ============================================================================
REM Voxel Engine - Test Runner and Game Launcher
REM ============================================================================
REM
REM This script provides an interactive menu to:
REM 1. Run various test suites (correctness, memory, performance, stress)
REM 2. Optionally launch the game after tests pass
REM 3. View test results and performance metrics
REM
REM Created: 2025-11-14
REM ============================================================================

setlocal enabledelayedexpansion

REM Check if build directory exists
if not exist "build" (
    echo [ERROR] Build directory not found!
    echo.
    echo Please build the project first:
    echo    1. mkdir build
    echo    2. cd build
    echo    3. cmake ..
    echo    4. cmake --build . --config Release
    echo.
    pause
    exit /b 1
)

:MAIN_MENU
cls
echo ============================================================================
echo                    VOXEL ENGINE - TEST RUNNER
echo ============================================================================
echo.
echo Select which tests to run:
echo.
echo   [1] Run ALL tests (recommended before release)
echo   [2] Run FAST tests only (quick check before commit)
echo   [3] Run CORRECTNESS tests only
echo   [4] Run MEMORY LEAK tests only
echo   [5] Run PERFORMANCE tests only
echo   [6] Run STRESS tests only
echo   [7] Skip tests and launch game directly
echo   [0] Exit
echo.
echo ============================================================================
set /p choice="Enter your choice (0-7): "

if "%choice%"=="0" goto END
if "%choice%"=="1" goto RUN_ALL_TESTS
if "%choice%"=="2" goto RUN_FAST_TESTS
if "%choice%"=="3" goto RUN_CORRECTNESS_TESTS
if "%choice%"=="4" goto RUN_MEMORY_TESTS
if "%choice%"=="5" goto RUN_PERFORMANCE_TESTS
if "%choice%"=="6" goto RUN_STRESS_TESTS
if "%choice%"=="7" goto LAUNCH_GAME

echo [ERROR] Invalid choice. Please enter a number between 0 and 7.
timeout /t 2 >nul
goto MAIN_MENU

REM ============================================================================
REM Test Execution Functions
REM ============================================================================

:RUN_ALL_TESTS
cls
echo ============================================================================
echo                         RUNNING ALL TESTS
echo ============================================================================
echo.
echo This will run:
echo   - Correctness tests (6 tests, ~2 seconds)
echo   - Memory leak tests (6 tests, ~30-60 seconds)
echo   - Performance tests (7 tests, ~10 seconds)
echo   - Stress tests (8 tests, ~10 seconds)
echo.
echo Total time: ~1-2 minutes
echo.
echo ============================================================================
echo.

cd build
ctest -C Release -V
set TEST_RESULT=%ERRORLEVEL%
cd ..

if %TEST_RESULT% equ 0 (
    echo.
    echo ============================================================================
    echo                    ALL TESTS PASSED! ✓
    echo ============================================================================
    goto ASK_LAUNCH_GAME
) else (
    echo.
    echo ============================================================================
    echo                    TESTS FAILED! ✗
    echo ============================================================================
    echo.
    echo Some tests failed. Do NOT commit or ship until fixed.
    echo Check the output above for error details.
    echo.
    pause
    goto MAIN_MENU
)

:RUN_FAST_TESTS
cls
echo ============================================================================
echo                      RUNNING FAST TESTS
echo ============================================================================
echo.
echo Running quick tests (correctness + basic checks)...
echo Time: ~10 seconds
echo.

cd build
ctest -C Release -L fast -V
set TEST_RESULT=%ERRORLEVEL%
cd ..

if %TEST_RESULT% equ 0 (
    echo.
    echo ============================================================================
    echo                   FAST TESTS PASSED! ✓
    echo ============================================================================
    goto ASK_LAUNCH_GAME
) else (
    echo.
    echo ============================================================================
    echo                   FAST TESTS FAILED! ✗
    echo ============================================================================
    pause
    goto MAIN_MENU
)

:RUN_CORRECTNESS_TESTS
cls
echo ============================================================================
echo                    RUNNING CORRECTNESS TESTS
echo ============================================================================
echo.
echo Testing:
echo   - Deterministic generation (same seed = same terrain)
echo   - Chunk state transitions
echo   - Block access bounds
echo   - Metadata persistence
echo.

cd build
ctest -C Release -R ChunkCorrectness -V
set TEST_RESULT=%ERRORLEVEL%
cd ..

if %TEST_RESULT% equ 0 (
    echo.
    echo ============================================================================
    echo                CORRECTNESS TESTS PASSED! ✓
    echo ============================================================================
    goto ASK_LAUNCH_GAME
) else (
    echo.
    echo ============================================================================
    echo                CORRECTNESS TESTS FAILED! ✗
    echo ============================================================================
    echo.
    echo CRITICAL: World generation is producing inconsistent results!
    echo This means terrain will be different each time with the same seed.
    echo.
    pause
    goto MAIN_MENU
)

:RUN_MEMORY_TESTS
cls
echo ============================================================================
echo                    RUNNING MEMORY LEAK TESTS
echo ============================================================================
echo.
echo Testing for memory leaks during:
echo   - Chunk load/unload cycles (100x)
echo   - World load/unload cycles (50x)
echo   - Large world cleanup
echo.
echo This may take 30-60 seconds...
echo.

cd build
ctest -C Release -R MemoryLeaks -V
set TEST_RESULT=%ERRORLEVEL%
cd ..

if %TEST_RESULT% equ 0 (
    echo.
    echo ============================================================================
    echo                  MEMORY TESTS PASSED! ✓
    echo ============================================================================
    echo.
    echo No memory leaks detected.
    goto ASK_LAUNCH_GAME
) else (
    echo.
    echo ============================================================================
    echo                  MEMORY TESTS FAILED! ✗
    echo ============================================================================
    echo.
    echo WARNING: Memory leaks detected!
    echo This will cause crashes after 10-30 minutes of gameplay.
    echo.
    pause
    goto MAIN_MENU
)

:RUN_PERFORMANCE_TESTS
cls
echo ============================================================================
echo                   RUNNING PERFORMANCE TESTS
echo ============================================================================
echo.
echo Checking performance gates:
echo   - Chunk generation: ^< 5ms
echo   - Mesh generation: ^< 3ms
echo   - Block access: ^< 10 µs
echo   - World loading: ^< 20ms per chunk
echo.

cd build
ctest -C Release -R Performance -V
set TEST_RESULT=%ERRORLEVEL%
cd ..

if %TEST_RESULT% equ 0 (
    echo.
    echo ============================================================================
    echo                PERFORMANCE TESTS PASSED! ✓
    echo ============================================================================
    echo.
    echo All performance gates met. Game will run smoothly.
    goto ASK_LAUNCH_GAME
) else (
    echo.
    echo ============================================================================
    echo                PERFORMANCE TESTS FAILED! ✗
    echo ============================================================================
    echo.
    echo WARNING: Performance gates exceeded!
    echo Players will experience stuttering during chunk loading.
    echo.
    pause
    goto MAIN_MENU
)

:RUN_STRESS_TESTS
cls
echo ============================================================================
echo                      RUNNING STRESS TESTS
echo ============================================================================
echo.
echo Testing edge cases:
echo   - Rapid teleportation
echo   - World boundary conditions
echo   - Massive block modifications
echo   - Extreme world sizes
echo.

cd build
ctest -C Release -R Stress -V
set TEST_RESULT=%ERRORLEVEL%
cd ..

if %TEST_RESULT% equ 0 (
    echo.
    echo ============================================================================
    echo                   STRESS TESTS PASSED! ✓
    echo ============================================================================
    goto ASK_LAUNCH_GAME
) else (
    echo.
    echo ============================================================================
    echo                   STRESS TESTS FAILED! ✗
    echo ============================================================================
    echo.
    echo Note: Stress test failures are edge cases that may be acceptable.
    echo Review the specific failure before deciding to ship.
    echo.
    pause
    goto MAIN_MENU
)

REM ============================================================================
REM Game Launch Functions
REM ============================================================================

:ASK_LAUNCH_GAME
echo.
echo ============================================================================
echo                        LAUNCH GAME?
echo ============================================================================
echo.
set /p launch_game="Do you want to launch the game now? (Y/N): "

if /i "%launch_game%"=="Y" goto LAUNCH_GAME
if /i "%launch_game%"=="N" goto MAIN_MENU
echo Invalid input. Please enter Y or N.
timeout /t 2 >nul
goto ASK_LAUNCH_GAME

:LAUNCH_GAME
cls
echo ============================================================================
echo                      LAUNCHING VOXEL ENGINE
echo ============================================================================
echo.

if not exist "build\Release\voxel-engine.exe" (
    echo [ERROR] Game executable not found!
    echo Expected location: build\Release\voxel-engine.exe
    echo.
    echo Please build the project first:
    echo    cd build
    echo    cmake --build . --config Release
    echo.
    pause
    goto MAIN_MENU
)

echo Starting game...
echo.
echo The game will run until you close it by pressing the X button.
echo Upon closing, you should see:
echo.
echo   Exiting main()...
echo   Destroying World...
echo   World destroyed (XXXXX chunks)
echo.
echo ============================================================================
echo.

cd build\Release
voxel-engine.exe
set GAME_EXIT_CODE=%ERRORLEVEL%
cd ..\..

echo.
echo ============================================================================
echo                         GAME EXITED
echo ============================================================================
echo.
echo Exit code: %GAME_EXIT_CODE%
echo.

if %GAME_EXIT_CODE% equ 0 (
    echo Game closed normally. All systems shut down cleanly.
) else (
    echo Warning: Game exited with error code %GAME_EXIT_CODE%
    echo Check console output above for any error messages.
)

echo.
echo Press any key to return to menu...
pause >nul
goto MAIN_MENU

REM ============================================================================
REM Exit
REM ============================================================================

:END
cls
echo ============================================================================
echo                   THANK YOU FOR TESTING!
echo ============================================================================
echo.
echo Remember:
echo   - Run fast tests before every commit
echo   - Run all tests before release
echo   - Check memory leaks before shipping
echo.
echo Happy developing! 🚀
echo.
timeout /t 3 >nul
exit /b 0
