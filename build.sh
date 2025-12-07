#!/bin/bash
set -e

if [ "$1" == "--clean" ]; then
    echo "Cleaning build directory..."
    rm -rf build
fi

echo "Building pixel_blit_firmware..."

# Create build directory
mkdir -p build
cd build

# Configure
echo "Configuring with CMake..."
cmake -G "Ninja" ..

# Build
echo "Building with Ninja..."
ninja

echo "Build complete!"
echo "Artifacts in build/:"
ls -F pixel_blit_firmware.uf2 pixel_blit_firmware.elf
