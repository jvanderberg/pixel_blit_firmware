#!/bin/bash
# Build firmware (without flashing)
set -e

echo "Building firmware..."
cargo build --release

echo "Converting to UF2..."
picotool uf2 convert target/thumbv8m.main-none-eabihf/release/pixel_blit_firmware -t elf target/pixel_blit_firmware.uf2 --family rp2350-arm-s --abs-block

echo "Build complete!"
echo "  Binary: target/thumbv8m.main-none-eabihf/release/pixel_blit_firmware"
echo "  UF2:    target/pixel_blit_firmware.uf2"

# Show size
arm-none-eabi-size target/thumbv8m.main-none-eabihf/release/pixel_blit_firmware 2>/dev/null || true
