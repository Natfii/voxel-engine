#!/bin/bash
# ============================================================================
# Linux Build Script for Voxel Engine (Vulkan)
# ============================================================================

set -e  # Exit on error

echo ""
echo "============================================================================"
echo "  Voxel Engine - Linux Build Script"
echo "============================================================================"
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check for -clean flag
if [ "$1" == "-clean" ] || [ "$1" == "--clean" ]; then
    echo -e "${YELLOW}[CLEAN] Removing build directory and compiled shaders...${NC}"
    if [ -d "build" ]; then
        echo "Deleting build directory..."
        rm -rf build
    fi
    if ls shaders/*.spv 1> /dev/null 2>&1; then
        echo "Deleting compiled shaders..."
        rm -f shaders/*.spv
    fi
    echo -e "${GREEN}Clean complete!${NC}"
    echo ""
fi

# Check if Vulkan SDK or system Vulkan is available
echo "[1/6] Checking for Vulkan..."
if [ -n "$VULKAN_SDK" ]; then
    echo -e "${GREEN}Vulkan SDK found at: $VULKAN_SDK${NC}"
elif pkg-config --exists vulkan; then
    echo -e "${GREEN}System Vulkan found via pkg-config${NC}"
elif [ -f "/usr/include/vulkan/vulkan.h" ]; then
    echo -e "${GREEN}Vulkan headers found in /usr/include/vulkan/${NC}"
else
    echo -e "${RED}[ERROR] Vulkan not found!${NC}"
    echo ""
    echo "Please install Vulkan SDK or system packages:"
    echo ""
    echo "Ubuntu/Debian:"
    echo "  sudo apt install vulkan-tools libvulkan-dev vulkan-validationlayers-dev"
    echo ""
    echo "Arch Linux:"
    echo "  sudo pacman -S vulkan-devel"
    echo ""
    echo "Or download Vulkan SDK from:"
    echo "  https://vulkan.lunarg.com/"
    echo ""
    exit 1
fi
echo ""

# Check for required system libraries
echo "[2/6] Checking for required libraries..."
MISSING_LIBS=()

if ! pkg-config --exists glfw3; then
    MISSING_LIBS+=("glfw3")
fi

if ! pkg-config --exists yaml-cpp; then
    echo -e "${YELLOW}Warning: yaml-cpp not found in system, will use bundled version${NC}"
fi

if [ ${#MISSING_LIBS[@]} -ne 0 ]; then
    echo -e "${RED}[ERROR] Missing required libraries: ${MISSING_LIBS[*]}${NC}"
    echo ""
    echo "Ubuntu/Debian:"
    echo "  sudo apt install libglfw3-dev"
    echo ""
    echo "Arch Linux:"
    echo "  sudo pacman -S glfw"
    echo ""
    exit 1
fi
echo -e "${GREEN}All required libraries found.${NC}"
echo ""

# Check if shaders folder exists
echo "[3/6] Checking shaders directory..."
if [ ! -d "shaders" ]; then
    echo "Creating shaders directory..."
    mkdir -p shaders
    echo -e "${GREEN}Shaders directory created.${NC}"
else
    echo -e "${GREEN}Shaders directory exists.${NC}"
fi
echo ""

# Compile shaders
echo "[4/6] Compiling shaders..."
cd shaders

SHADER_COMPILER=""
if command -v glslc &> /dev/null; then
    SHADER_COMPILER="glslc"
    echo "Using glslc to compile shaders..."
elif command -v glslangValidator &> /dev/null; then
    SHADER_COMPILER="glslangValidator"
    echo "Using glslangValidator to compile shaders..."
elif [ -n "$VULKAN_SDK" ] && [ -f "$VULKAN_SDK/bin/glslc" ]; then
    SHADER_COMPILER="$VULKAN_SDK/bin/glslc"
    echo "Using glslc from Vulkan SDK..."
elif [ -n "$VULKAN_SDK" ] && [ -f "$VULKAN_SDK/bin/glslangValidator" ]; then
    SHADER_COMPILER="$VULKAN_SDK/bin/glslangValidator"
    echo "Using glslangValidator from Vulkan SDK..."
else
    echo -e "${RED}[ERROR] No shader compiler found!${NC}"
    echo ""
    echo "Please install shader compiler:"
    echo "  Ubuntu/Debian: sudo apt install glslang-tools"
    echo "  Arch Linux: sudo pacman -S shaderc"
    echo "  Or install Vulkan SDK"
    cd ..
    exit 1
fi

if [[ "$SHADER_COMPILER" == *"glslc"* ]]; then
    $SHADER_COMPILER shader.vert -o vert.spv
    $SHADER_COMPILER shader.frag -o frag.spv
    $SHADER_COMPILER line.vert -o line_vert.spv
    $SHADER_COMPILER line.frag -o line_frag.spv
    $SHADER_COMPILER skybox.vert -o skybox_vert.spv
    $SHADER_COMPILER skybox.frag -o skybox_frag.spv
else
    $SHADER_COMPILER -V shader.vert -o vert.spv
    $SHADER_COMPILER -V shader.frag -o frag.spv
    $SHADER_COMPILER -V line.vert -o line_vert.spv
    $SHADER_COMPILER -V line.frag -o line_frag.spv
    $SHADER_COMPILER -V skybox.vert -o skybox_vert.spv
    $SHADER_COMPILER -V skybox.frag -o skybox_frag.spv
fi

if [ $? -eq 0 ]; then
    echo -e "${GREEN}Shaders compiled successfully!${NC}"
    echo "  - vert.spv"
    echo "  - frag.spv"
    echo "  - line_vert.spv"
    echo "  - line_frag.spv"
    echo "  - skybox_vert.spv"
    echo "  - skybox_frag.spv"
else
    echo -e "${RED}[ERROR] Shader compilation failed!${NC}"
    cd ..
    exit 1
fi

cd ..
echo ""

# Create build directory
echo "[5/6] Setting up build directory..."
if [ ! -d "build" ]; then
    mkdir build
    echo -e "${GREEN}Build directory created.${NC}"
else
    echo -e "${GREEN}Build directory exists.${NC}"
fi
echo ""

# Run CMake and build
echo "[6/6] Running CMake and building project..."
cd build

# Run CMake
cmake .. -DCMAKE_BUILD_TYPE=Release
if [ $? -ne 0 ]; then
    echo -e "${RED}[ERROR] CMake configuration failed!${NC}"
    cd ..
    exit 1
fi

echo ""
echo "Building project..."
make -j$(nproc)

if [ $? -ne 0 ]; then
    echo -e "${RED}[ERROR] Build failed!${NC}"
    cd ..
    exit 1
fi

cd ..

echo ""
echo "============================================================================"
echo -e "${GREEN}  BUILD SUCCESSFUL!${NC}"
echo "============================================================================"
echo ""
echo "Executable location: build/voxel-engine"
echo ""
echo "To run the game:"
echo "  cd build"
echo "  ./voxel-engine"
echo ""
echo "Or simply run: ./run.sh"
echo ""
