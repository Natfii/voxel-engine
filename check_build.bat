@echo off
REM ============================================================================
REM Build Diagnostic Script - Check if tests are properly configured
REM ============================================================================

echo ============================================================================
echo   BUILD DIAGNOSTIC
echo ============================================================================
echo.

echo [1/5] Checking if build directory exists...
if not exist "build" (
    echo [ERROR] Build directory does not exist!
    echo You need to run: build.bat
    echo.
    pause
    exit /b 1
)
echo [OK] Build directory exists
echo.

echo [2/5] Checking if tests subdirectory exists in build...
if not exist "build\tests" (
    echo [ERROR] build\tests directory does not exist!
    echo This means CMake did not process the tests.
    echo.
    echo Possible causes:
    echo   1. CMake ran before add_subdirectory^(tests^) was added
    echo   2. CMake cache is stale
    echo.
    echo SOLUTION: Run clean reconfiguration
    echo   build.bat --clean
    echo.
    pause
    exit /b 1
)
echo [OK] build\tests directory exists
echo.

echo [3/5] Checking for test executables...
set TESTS_FOUND=0

if exist "build\tests\Release\test_chunk_correctness.exe" (
    echo [OK] test_chunk_correctness.exe found
    set /a TESTS_FOUND+=1
)

if exist "build\tests\Release\test_memory_leaks.exe" (
    echo [OK] test_memory_leaks.exe found
    set /a TESTS_FOUND+=1
)

if exist "build\tests\Release\test_performance.exe" (
    echo [OK] test_performance.exe found
    set /a TESTS_FOUND+=1
)

if exist "build\tests\Release\test_stress.exe" (
    echo [OK] test_stress.exe found
    set /a TESTS_FOUND+=1
)

if %TESTS_FOUND%==0 (
    echo [ERROR] No test executables found!
    echo Expected location: build\tests\Release\test_*.exe
    echo.
    echo SOLUTION: Build the project
    echo   cd build
    echo   cmake --build . --config Release
    echo.
    pause
    exit /b 1
)

echo [OK] Found %TESTS_FOUND%/4 test executables
echo.

echo [4/5] Checking CTestTestfile.cmake...
if not exist "build\tests\CTestTestfile.cmake" (
    echo [ERROR] build\tests\CTestTestfile.cmake not found!
    echo CMake did not generate test configuration.
    echo.
    echo SOLUTION: Reconfigure CMake
    echo   build.bat --clean
    echo.
    pause
    exit /b 1
)
echo [OK] CTestTestfile.cmake exists
echo.

echo [5/5] Checking if tests are registered...
findstr /C:"add_test" "build\tests\CTestTestfile.cmake" >nul 2>&1
if errorlevel 1 (
    echo [ERROR] No tests registered in CTestTestfile.cmake!
    echo The file exists but has no add_test^(^) calls.
    echo.
    echo SOLUTION: Reconfigure CMake
    echo   build.bat --clean
    echo.
    pause
    exit /b 1
)
echo [OK] Tests are registered in CTest
echo.

echo ============================================================================
echo   ALL CHECKS PASSED!
echo ============================================================================
echo.
echo Your build is properly configured. You can now run:
echo   cd build
echo   ctest -C Release -V
echo.
echo Or use: test_and_run.bat
echo.
pause
