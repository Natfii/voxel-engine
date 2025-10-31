# Shader Compilation

This directory contains GLSL shaders that need to be compiled to SPIR-V format for Vulkan.

## Requirements

You need the Vulkan SDK installed, which includes shader compilers:
- **glslc** (preferred) - Part of shaderc
- **glslangValidator** (alternative)

Download from: https://vulkan.lunarg.com/

## Compilation

Run the compile script:
```bash
cd shaders
chmod +x compile.sh
./compile.sh
```

This will generate:
- `vert.spv` - Compiled vertex shader
- `frag.spv` - Compiled fragment shader

These compiled shaders are required for the application to run.

## Manual Compilation

If you prefer to compile manually:

```bash
# Using glslc
glslc shader.vert -o vert.spv
glslc shader.frag -o frag.spv

# Using glslangValidator
glslangValidator -V shader.vert -o vert.spv
glslangValidator -V shader.frag -o frag.spv
```
