#!/bin/bash
# ============================================================================
# Linux Run Script for Voxel Engine
# ============================================================================

if [ ! -f "build/voxel-engine" ]; then
    echo "Executable not found!"
    echo ""
    echo "Please build the project first by running: ./build.sh"
    echo ""
    exit 1
fi

echo "Starting Voxel Engine..."
echo ""
cd build
./voxel-engine
cd ..
