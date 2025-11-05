#!/bin/bash

# Compile shaders to SPIR-V
# Requires Vulkan SDK to be installed with glslc or glslangValidator

if command -v glslc &> /dev/null; then
    echo "Using glslc to compile shaders..."
    glslc shader.vert -o vert.spv
    glslc shader.frag -o frag.spv
    glslc line.vert -o line_vert.spv
    glslc line.frag -o line_frag.spv
    glslc skybox.vert -o skybox_vert.spv
    glslc skybox.frag -o skybox_frag.spv
    echo "Shaders compiled successfully with glslc!"
elif command -v glslangValidator &> /dev/null; then
    echo "Using glslangValidator to compile shaders..."
    glslangValidator -V shader.vert -o vert.spv
    glslangValidator -V shader.frag -o frag.spv
    glslangValidator -V line.vert -o line_vert.spv
    glslangValidator -V line.frag -o line_frag.spv
    glslangValidator -V skybox.vert -o skybox_vert.spv
    glslangValidator -V skybox.frag -o skybox_frag.spv
    echo "Shaders compiled successfully with glslangValidator!"
else
    echo "ERROR: No shader compiler found!"
    echo "Please install the Vulkan SDK which includes glslc or glslangValidator"
    echo "Download from: https://vulkan.lunarg.com/"
    exit 1
fi
