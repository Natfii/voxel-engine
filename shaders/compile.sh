#!/bin/bash

# Compile shaders to SPIR-V
# Requires Vulkan SDK to be installed with glslc or glslangValidator

if command -v glslc &> /dev/null; then
    echo "Using glslc to compile shaders..."
    glslc shader.vert -o vert.spv
    glslc shader.frag -o frag.spv
    echo "Shaders compiled successfully with glslc!"
elif command -v glslangValidator &> /dev/null; then
    echo "Using glslangValidator to compile shaders..."
    glslangValidator -V shader.vert -o vert.spv
    glslangValidator -V shader.frag -o frag.spv
    echo "Shaders compiled successfully with glslangValidator!"
else
    echo "ERROR: No shader compiler found!"
    echo "Please install the Vulkan SDK which includes glslc or glslangValidator"
    echo "Download from: https://vulkan.lunarg.com/"
    exit 1
fi
