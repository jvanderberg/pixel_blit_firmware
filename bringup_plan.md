# Pixel_Blit Board Bring-Up Checklist

Use this walkthrough when validating each new assembly. Record results per serial number so we can trace failures.

## 1. Visual & Continuity Inspection
- Inspect both PCB sides under magnification for solder bridges, tombstones, lifted pads, or skewed QFN/RJ45 connectors.
- Confirm power components (TPS63070, ME6217C33, inductors, sense resistors) and the RP2350 are oriented correctly.
- Meter VIN, 5V, 3V3, and DIP ladder nets for shorts to ground; expect >10 kΩ.
- Verify the DIP ladder node (ADC0) floats near the mid-rail (~3.2 V) with all DIP switches open.

## 2. Power Subsystem Bring-Up
- Connect a current-limited bench supply (~12 V, 200 mA limit) to the 12 V terminals.
- Check TPS63070 output: should regulate to 5.0 V (±2%). Monitor ripple with an oscilloscope.
- Check ME6217C33 output: should be 3.3 V feeding the MCU, flash, LVDS parts.
- Allow the RP2350 to boot and confirm the internal 1.1 V rail is present (test pad or via SWD if available).
- Only remove the current limit once all rails look nominal.

## 3. MCU, Flash, USB, SD
- Enter USB boot mode and ensure the board enumerates as a RP2350 device.
- Use `picotool info` to confirm the 16 MB QSPI flash responds.
- Flash a minimal UF2 (e.g., LED blink) to exercise USB and GPIO outputs.
- Run a Pico-SDK FATFS sample to mount the microSD slot; log read/write success.

## 4. Board Address Ladder Verification
- Flash `board address prototype/temp_board_address.c`.
- Connect USB serial, record the ADC code/margin output with all DIP combinations (00..15).
- Ensure decoded address matches the physical DIP settings and margins stay > the measured noise floor.
- If margins are tight, update the `level_codes` table and reflash.

## 5. LED Output Path
- Load the `cblinken` demo (single-strip chase pattern) and start with one WS2812 string on `String0`.
- Verify level shifters output ~5 V logic and there are no ringing issues on the scope.
- Incrementally attach additional strings; monitor 5 V current draw and TPS63070 temperature.
- Confirm DMA double-buffering keeps up (no underflow logs) and color order is correct on each strip.

## 6. UI & Inputs
- While the LED demo runs, exercise tactile buttons and DIP switches; confirm the firmware reacts or that GPIO readings toggle as expected via SWD logging.
- Attach the IR receiver and run `ir_control.c`; ensure NEC remote codes appear in the queue and the handler can toggle a test variable.
- If an I²C display is available, run a simple write/read test over `DISP_SDA/SDL`.

## 7. LVDS Networking
- Prepare two boards. Build one as `PARALLEL_SENDER`, the other as `PARALLEL_RECEIVER` using `lvds/blink.c`.
- Connect via short Cat5/Cat6. On the receiver, watch for stable Mbps, zero failures, and correct board-ID filtering.
- Repeat with longer cables; log any error rate increases and capture eye diagrams if possible.
- Verify `DATA_IN_EN` gating obeys the DIP-set board address (non-zero boards listen only).

## 8. Integrated Pixel_Blit Firmware
- Flash the full `pixel_blit_firmware.c` image on board 0 and a downstream board.
- Confirm LVDS streaming delivers frames, `DATA_IN_EN` enables inputs on non-zero boards, and all 32 outputs update at 800 kHz with no flicker.
- Run at maximum intended load; monitor 12 V input current, buck-boost thermals, and ensure screw terminals stay cool.

## 9. Documentation & Sign-Off
- Log measured voltages, firmware versions, and any rework performed.
- Store oscilloscope captures or photos for anomalies.
- Only release boards that pass every section; otherwise, tag them with the failing step for rework.
