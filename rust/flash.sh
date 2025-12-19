#!/bin/bash
set -e

echo "Building firmware..."
cargo build --release

echo "Converting to UF2..."
picotool uf2 convert target/thumbv8m.main-none-eabihf/release/pixel_blit_firmware -t elf target/pixel_blit_firmware.uf2 --family rp2350-arm-s --abs-block

# Try to trigger bootloader via 1200 baud touch (if device is running)
SERIAL_PORT=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)
if [ -n "$SERIAL_PORT" ]; then
    echo "Triggering bootloader via 1200 baud touch on $SERIAL_PORT..."
    python3 -c "
import serial
import time
try:
    ser = serial.Serial('$SERIAL_PORT', 1200)
    ser.dtr = True
    time.sleep(0.1)
    ser.dtr = False
    ser.close()
except Exception as e:
    print(f'Note: {e}')
" 2>/dev/null || echo "  (pyserial not available, skipping)"
    sleep 1
fi

echo "Flashing..."
picotool load target/pixel_blit_firmware.uf2

echo "Rebooting..."
picotool reboot

echo "Done!"
