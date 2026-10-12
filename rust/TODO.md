# Rust Port Status & TODO

Tracking progress on porting pixel_blit firmware from C to Rust/Embassy.

## Current Status: ~80% Complete

The core architecture is working. Main remaining work is FSEQ playback and polish.

---

## Completed

### Core Architecture
- [x] Redux-style state management via `reducto` crate
- [x] AppState with all menu entries, run states, boot states
- [x] Action enum for all events (buttons, SD card, playback, etc.)
- [x] Pure reducer with full menu navigation logic
- [x] Host-runnable tests (`./test.sh`)

### Hardware Drivers
- [x] **LED Driver** - 32-string parallel PIO output with double buffering
  - Bit-plane encoding for DMA output
  - Per-string config (length, color order)
  - Frame generator trait for swappable animations
  - HSV/RGB color conversion
  - Token-based ownership for safe buffer access (see CLAUDE.md)
- [x] **Flash Persistence** - Settings storage with CRC32 validation
  - Uses Async flash with DMA for reliable writes
  - 2-second debounce to reduce flash wear
- [x] **Display** - SH1106 OLED via I2C (GPIO 46-47)
- [x] **Buttons** - Async edge detection with 200ms debounce (GPIO 43, 45)
- [x] **SD Card** - SPI reader with config.csv and .fseq scanning
- [x] **USB CDC** - Debug output via `usb_println!`

### Views (Display UI)
- [x] Boot screen (loading/error states)
- [x] Main menu with 7 entries
- [x] Info view (string configs, scrollable)
- [x] Board Address view
- [x] SD Card view (file list, playback status)
- [x] Rainbow Test view (running, string selection)
- [x] String Length view (pixel position, string selection)
- [x] Brightness view
- [x] Shutdown confirmation

### Frame Generators
- [x] RainbowGenerator - Rainbow on selected string, red on others
- [x] StringLengthGenerator - Single pixel for measuring string length

---

## In Progress

### FSEQ Playback (Priority 1)
- [x] File scanning and listing
- [x] Playback state in reducer (is_playing, playing_index, auto_loop)
- [x] **FSEQ parser** - Parse header, channel data (uncompressed v2 only)
- [x] **FseqGenerator** - Frame generator that receives pixel data from SD task
- [x] **Playback task** - Integrated into SD card task with FPS limiting
- [x] Auto-loop support (loops single file, auto_loop advances to next file)
- [ ] Resume after power cycle (settings already persist playing_index)
- [ ] Test on actual hardware with real .fseq files

---

## Not Started

### IR Remote (Priority 2)
- [ ] GPIO 42 input with NEC protocol decoding
- [ ] Actions: PowerToggle, BrightnessUp/Down, FseqNext, AutoToggle
- [ ] Interrupt-based edge detection

### Board Address ADC (Priority 3)
- [ ] GPIO 40 (ADC0) voltage ladder reading
- [ ] DIP switch decoding (16 levels)
- [ ] Auto-detect board ID on boot

### LVDS Networking (Priority 4)
- [ ] Inter-board daisy-chain protocol
- [ ] PIO programs for TX/RX
- [ ] Board filtering by ID

### Polish
- [ ] FPS reporting to UI (Action::RainbowFrameComplete exists but unused)
- [ ] Brightness scaling in LED output
- [ ] Fix flash persistence (currently fails due to XIP conflict)
- [ ] Gamma correction table

### Error Handling
- [ ] **Global error screen** - Any code can trigger a visible error display
  - Add `Action::ShowError { message: &'static str }`
  - Error view shows message, clears on button press
  - Replace silent `error!()` logs with user-visible errors where appropriate
  - Useful for: SD card failures, config parse errors, FSEQ read errors

---

## File Structure

```
rust/src/
├── main.rs              # Entry point, effect handlers
├── tasks.rs             # Async tasks (buttons, SD, LED, FSEQ playback)
├── generators.rs        # Frame generators (Rainbow, StringLength, Fseq)
├── lib.rs               # Library exports
├── state.rs             # AppState, enums, constants
├── action.rs            # Action enum
├── effect.rs            # Side effect types
├── reducer.rs           # Pure state transitions
├── reducer_tests.rs     # Host-runnable tests
├── storage.rs           # Flash persistence
├── display.rs           # Display trait + MockDisplay
├── usb.rs               # USB CDC for debug output
├── sdcard.rs            # SD card reader, config parser
├── fseq_parser.rs       # FSEQ v2 streaming parser
├── led_driver/
│   ├── mod.rs           # Module exports
│   ├── buffer.rs        # Bit-plane buffer
│   ├── color.rs         # RGB/HSV conversion
│   ├── output.rs        # PIO + DMA output
│   └── runner.rs        # Frame generator loop
└── views/
    ├── mod.rs           # RootView dispatcher
    ├── boot.rs
    ├── main_menu.rs
    ├── info.rs
    ├── board_address.rs
    ├── sd_card.rs
    ├── rainbow_test.rs
    ├── string_length.rs
    ├── brightness.rs
    └── shutdown.rs
```

---

## Commands

```bash
./test.sh      # Run reducer/view tests on host
./build.sh     # Build firmware + convert to UF2
./flash.sh     # Build and flash to device
```

---

## Notes

### Embassy Quirks
- Must use git version of embassy-rp (crates.io 0.4 has RP2350 timer bugs)
- UF2 conversion needs `--abs-block` for RP2350 E10 errata
- memory.x requires RP2350-specific sections (.start_block, .bi_entries, .end_block)

### Notes
- Core 0 only (Core 1 reserved for future LVDS networking)
- Flash operations use Async mode with DMA for XIP safety

### Hardware Pin Map
| GPIO | Function |
|------|----------|
| 0-31 | LED string outputs |
| 36-39 | SD card SPI (MISO, CS, SCK, MOSI) |
| 40 | Board address ADC (not implemented) |
| 42 | IR receiver (not implemented) |
| 43 | Select button |
| 45 | Next button |
| 46-47 | I2C display (SDA, SCL) |
