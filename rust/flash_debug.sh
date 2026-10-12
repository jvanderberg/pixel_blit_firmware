#!/bin/bash
# Flash debug build and reset - then attach with VS Code "Attach (probe-rs)"
set -e
cd "$(dirname "$0")"
cargo build
probe-rs download --chip RP2350 target/thumbv8m.main-none-eabihf/debug/pixel_blit_firmware --reset
echo "Flashed. Now run 'Attach (probe-rs)' in VS Code."
