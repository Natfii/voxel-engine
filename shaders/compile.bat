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
    if errorlevel 1 goto :compile_error
    "%VULKAN_SDK%\Bin\glslc.exe" shader.frag -o frag.spv
    if errorlevel 1 goto :compile_error
    "%VULKAN_SDK%\Bin\glslc.exe" line.vert -o line_vert.spv
    if errorlevel 1 goto :compile_error
    "%VULKAN_SDK%\Bin\glslc.exe" line.frag -o line_frag.spv
    if errorlevel 1 goto :compile_error
    "%VULKAN_SDK%\Bin\glslc.exe" skybox.vert -o skybox_vert.spv
    if errorlevel 1 goto :compile_error
    "%VULKAN_SDK%\Bin\glslc.exe" skybox.frag -o skybox_frag.spv
    if errorlevel 1 goto :compile_error

    echo.
    echo SUCCESS! All shaders compiled:
    echo   - vert.spv
    echo   - frag.spv
    echo   - line_vert.spv
    echo   - line_frag.spv
    echo   - skybox_vert.spv
    echo   - skybox_frag.spv
    echo.
    goto :success

) else if exist "%VULKAN_SDK%\Bin\glslangValidator.exe" (
    echo Using glslangValidator to compile shaders...
    "%VULKAN_SDK%\Bin\glslangValidator.exe" -V shader.vert -o vert.spv
    if errorlevel 1 goto :compile_error
    "%VULKAN_SDK%\Bin\glslangValidator.exe" -V shader.frag -o frag.spv
    if errorlevel 1 goto :compile_error
    "%VULKAN_SDK%\Bin\glslangValidator.exe" -V line.vert -o line_vert.spv
    if errorlevel 1 goto :compile_error
    "%VULKAN_SDK%\Bin\glslangValidator.exe" -V line.frag -o line_frag.spv
    if errorlevel 1 goto :compile_error
    "%VULKAN_SDK%\Bin\glslangValidator.exe" -V skybox.vert -o skybox_vert.spv
    if errorlevel 1 goto :compile_error
    "%VULKAN_SDK%\Bin\glslangValidator.exe" -V skybox.frag -o skybox_frag.spv
    if errorlevel 1 goto :compile_error

    echo.
    echo SUCCESS! All shaders compiled:
    echo   - vert.spv
    echo   - frag.spv
    echo   - line_vert.spv
    echo   - line_frag.spv
    echo   - skybox_vert.spv
    echo   - skybox_frag.spv
    echo.
    goto :success

) else (
    echo ERROR: No shader compiler found in Vulkan SDK!
    echo Expected location: %VULKAN_SDK%\Bin\
    echo Please reinstall the Vulkan SDK and ensure "Shader Toolchain" is selected.
    pause
    exit /b 1
)

:compile_error
echo ERROR: Shader compilation failed!
pause
exit /b 1

:success
pause
exit /b 0
