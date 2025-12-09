#!/bin/bash
# Run all host tests

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
FAILED=0

echo "========================================"
echo "Running all tests"
echo "========================================"

# --- pb_led_driver tests ---
echo ""
echo "--- pb_led_driver tests ---"
BUILD_DIR="$SCRIPT_DIR/build_test"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

if [ ! -f "Makefile" ]; then
    echo "Configuring..."
    cmake -DPB_LED_DRIVER_TEST=ON "$SCRIPT_DIR/lib/pb_led_driver"
fi

make -j > /dev/null
./pb_led_driver_test || FAILED=1

# --- board_config tests ---
echo ""
echo "--- board_config tests ---"
BUILD_DIR="$SCRIPT_DIR/build_test_board_config"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

if [ ! -f "Makefile" ]; then
    echo "Configuring..."
    cmake "$SCRIPT_DIR/test/board_config"
fi

make -j > /dev/null
./test_board_config || FAILED=1

# --- Summary ---
echo ""
echo "========================================"
if [ $FAILED -eq 0 ]; then
    echo "All test suites passed!"
else
    echo "Some tests failed!"
    exit 1
fi
echo "========================================"
