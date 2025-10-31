@echo off
echo Compiling Vulkan shaders to SPIR-V...
echo.

REM Check if VULKAN_SDK is set
if not defined VULKAN_SDK (
    echo ERROR: VULKAN_SDK environment variable not set!
    echo Please install the Vulkan SDK from: https://vulkan.lunarg.com/
    echo After installation, restart your computer and try again.
    pause
    exit /b 1
)

echo Vulkan SDK found at: %VULKAN_SDK%
echo.

REM Try glslc first (preferred)
if exist "%VULKAN_SDK%\Bin\glslc.exe" (
    echo Using glslc to compile shaders...
    "%VULKAN_SDK%\Bin\glslc.exe" shader.vert -o vert.spv
    if errorlevel 1 (
        echo ERROR: Failed to compile vertex shader!
        pause
        exit /b 1
    )

    "%VULKAN_SDK%\Bin\glslc.exe" shader.frag -o frag.spv
    if errorlevel 1 (
        echo ERROR: Failed to compile fragment shader!
        pause
        exit /b 1
    )

    echo.
    echo SUCCESS! Shaders compiled:
    echo   - vert.spv
    echo   - frag.spv
    echo.

) else if exist "%VULKAN_SDK%\Bin\glslangValidator.exe" (
    echo Using glslangValidator to compile shaders...
    "%VULKAN_SDK%\Bin\glslangValidator.exe" -V shader.vert -o vert.spv
    if errorlevel 1 (
        echo ERROR: Failed to compile vertex shader!
        pause
        exit /b 1
    )

    "%VULKAN_SDK%\Bin\glslangValidator.exe" -V shader.frag -o frag.spv
    if errorlevel 1 (
        echo ERROR: Failed to compile fragment shader!
        pause
        exit /b 1
    )

    echo.
    echo SUCCESS! Shaders compiled:
    echo   - vert.spv
    echo   - frag.spv
    echo.

) else (
    echo ERROR: No shader compiler found in Vulkan SDK!
    echo Expected location: %VULKAN_SDK%\Bin\
    echo Please reinstall the Vulkan SDK and ensure "Shader Toolchain" is selected.
    pause
    exit /b 1
)

pause
