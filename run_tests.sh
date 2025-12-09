#!/bin/bash
# Run pb_led_driver host tests

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build_test"

# Create build directory if needed
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure if needed
if [ ! -f "Makefile" ]; then
    echo "Configuring test build..."
    cmake -DPB_LED_DRIVER_TEST=ON "$SCRIPT_DIR/lib/pb_led_driver"
fi

# Build
echo "Building tests..."
make -j

# Run
echo ""
./pb_led_driver_test
