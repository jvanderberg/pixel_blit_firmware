# Claude Instructions for pixel_blit_firmware

## Project Overview

This is firmware for a 32-channel addressable LED controller based on the RP2350B microcontroller. The firmware controls WS2811/WS2812 LED strings via PIO, with a menu-driven OLED interface.

## Build Instructions

Use the provided build script:
```bash
./build.sh
```

Or manually:
```bash
mkdir -p build && cd build
cmake -G Ninja ..
ninja
```

## Build System & Dependencies

### External Libraries
The project uses `FetchContent` to pull in the `no-OS-FatFS-SD-SPI-RPi-Pico` library.

**Integration Strategy (Important):**
The library is integrated by adding its **source files directly** to the executable target, rather than linking against a library target. This avoids CMake target namespace collisions and allows tighter control over configuration.

**Key Configuration Details:**
1.  **Source Directories:** We explicitly add sources from `FatFs_SPI/sd_driver`, `FatFs_SPI/ff15/source`, and `FatFs_SPI/src/glue.c`.
2.  **Include Paths:** We manually add include directories for `sd_driver`, `include`, and `ff15/source`. Note that `FetchContent` variables are lowercase (e.g., `${no-os-fatfs..._SOURCE_DIR}`).
3.  **RTC Stub:** We use a local `lib/sd_card/my_rtc.c` to stub `get_fattime()`. We do *not* link `hardware_rtc` or compile the library's `rtc.c` to avoid SDK include path issues.
4.  **Hardware Links:** The target must explicitly link `hardware_spi`.

### Architecture

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
- **GPIO 42**: IR receiver input (active low, NEC protocol)
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

4. **Single GPIO callback per core**: Pico SDK only allows ONE `gpio_set_irq_enabled_with_callback()` per core. All GPIO interrupts (buttons, IR, etc.) must share a combined ISR. Use `gpio_set_irq_enabled()` for additional pins after the initial callback setup.

## Memory Safety Patterns (CRITICAL)

When writing or refactoring embedded C code, watch for these patterns that cause memory corruption:

### 1. Pointer Lifetime Mismatches
**NEVER** store a pointer to a local/stack variable in a struct that outlives the function:
```c
// BAD - string_lengths is on stack, pointer becomes dangling after function returns
void setup_parser() {
    uint16_t string_lengths[32];  // Stack variable!
    layout.string_lengths = string_lengths;  // Pointer stored in struct
    parser_init(&layout);  // Parser keeps the pointer
}  // string_lengths destroyed here, parser has dangling pointer

// GOOD - use static or heap allocation for data that outlives the function
static uint16_t g_string_lengths[32];  // Static - lives forever
void setup_parser() {
    layout.string_lengths = g_string_lengths;
    parser_init(&layout);
}
```

### 2. DMA/Async Operation Teardown
**ALWAYS** wait for async operations to complete before freeing resources:
```c
// BAD - DMA may still be running when driver is destroyed
pb_show(driver);           // Starts DMA (async!)
pb_driver_deinit(driver);  // Destroys driver while DMA runs -> corruption

// GOOD - wait for DMA to complete
pb_show(driver);
pb_show_wait(driver);      // Block until DMA finishes
pb_driver_deinit(driver);  // Safe now
```

### 3. Multicore Memory Visibility
Static initializers run on Core 0. Core 1 may not see them immediately due to caching:
```c
// BAD - Core 1 might see uninitialized values
static volatile bool flag = false;  // Initialized by Core 0
void core1_main() {
    while (!flag) {}  // May never see the initialized value!
}

// GOOD - explicit initialization with memory barrier on Core 1
void core1_main() {
    flag = false;  // Explicit init
    __dmb();       // Memory barrier
    // Now safe to use
}
```

### 4. Struct Containing Pointers
When a struct contains a pointer field, copying the struct does NOT copy the pointed-to data:
```c
typedef struct {
    uint16_t* lengths;  // Pointer field
} layout_t;

// BAD - shallow copy, both point to same memory
layout_t a = {.lengths = array};
layout_t b = a;  // b.lengths == a.lengths (same pointer!)

// If caller expects deep copy, document it or copy explicitly
```

### 5. Refactoring Function Splits
When splitting a function into start/run/cleanup phases, check that:
- Local variables accessed across phases become static or are passed explicitly
- Pointers stored during "start" remain valid through "run" and "cleanup"
- Resources acquired in "start" are released in "cleanup" even on error paths

## Running Tests

The pb_led_driver library has host-based unit tests:
```bash
./run_tests.sh
```

## Reference Implementation

The AWG project at `../awg` is the reference implementation of the reactive architecture pattern. Key files:
- `../awg/include/AppState.h` - State container
- `../awg/include/Action.h` - Action definitions
- `../awg/src/StateReducer.cpp` - Pure reducer
- `../awg/src/SideEffects.cpp` - Hardware I/O
