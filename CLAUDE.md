# Claude Instructions for pixel_blit_firmware

## Project Overview

This is firmware for a 32-channel addressable LED controller based on the RP2350B microcontroller. The firmware controls WS2811/WS2812 LED strings via PIO, with a menu-driven OLED interface.

## Architecture

**IMPORTANT**: This project uses a Redux-inspired reactive architecture. Before modifying state management or adding features, read [docs/reactive_architecture.md](docs/reactive_architecture.md).

### Key Principles

1. **State is immutable**: Never modify AppState fields directly. Create a new state with an incremented version.

2. **Reducer is pure**: The reducer function must have NO side effects - no hardware access, no I/O, no timers. Same inputs must always produce same outputs.

3. **Side effects are isolated**: All hardware I/O (PIO, I2C, GPIO) must go in the SideEffects module, not in the reducer.

4. **Actions describe events**: Actions represent "what happened" (e.g., `ACTION_BUTTON_SELECT`), not "what to do".

### Data Flow

```
Hardware Events → Actions → Reducer → New State → SideEffects + View
```

## Code Conventions

- Don't add Claude by-line to commits
- Use `snake_case` for C functions and variables
- Use `UPPER_CASE` for constants and macros
- Keep reducer functions small and focused

## Hardware Details

- **GPIO 0-31**: LED string outputs (directly mapped: String N = GPIO N)
- **GPIO 43**: Select button (active low, needs debounce)
- **GPIO 45**: Next button (active low, needs debounce)
- **GPIO 46-47**: I2C OLED display (SH1106, 128x64)
- **PIO0**: Used by string_test
- **PIO1**: Used by rainbow_test (WS2811 output)

## WS2811/WS2812 Timing

- Bit rate: 800kHz (1.25µs per bit)
- 24 bits per pixel (RGB)
- Time per pixel: 30µs
- Reset pulse: minimum 80µs (use 200µs to be safe)
- 50 pixels = 1.5ms per frame, theoretical max ~600fps

## Common Pitfalls

1. **FIFO empty != data sent**: `pio_sm_is_tx_fifo_empty()` returns true when FIFO is empty, but data may still be shifting out of the OSR. Wait additional time after FIFO empties.

2. **Button debounce**: Use 200ms debounce minimum. The buttons have significant bounce.

3. **Blocking in main loop**: Avoid blocking calls in the main loop as they affect all tasks. Use timer-based scheduling instead.

## Reference Implementation

The AWG project at `../awg` is the reference implementation of the reactive architecture pattern. Key files:
- `../awg/include/AppState.h` - State container
- `../awg/include/Action.h` - Action definitions
- `../awg/src/StateReducer.cpp` - Pure reducer
- `../awg/src/SideEffects.cpp` - Hardware I/O
