# pb_led_driver - High-Speed Parallel LED Driver

A standalone library for driving up to 32 WS2811/WS2812 LED strings in parallel using a single PIO state machine on RP2350B.

**Validated Performance**: 32 strings × 50 pixels = 1600 LEDs @ 120 FPS

## Quick Start

```c
#include "pb_led_driver.h"

// Configure driver
pb_driver_config_t config = {
    .gpio_base = 0,
    .num_strings = 32,
    .max_pixel_length = 50,
    .frequency_hz = 800000,
    .color_order = PB_COLOR_ORDER_BRG,  // Check your LED type!
    .reset_us = 200,
    .pio_index = 1,
};

// Enable strings
for (int i = 0; i < 32; i++) {
    config.strings[i].length = 50;
    config.strings[i].enabled = true;
}

// Initialize
pb_driver_t* driver = pb_driver_init(&config);

// Set pixels and display
pb_set_pixel(driver, 0, 0, 10, 0xFF0000);  // Red pixel
pb_show(driver);
```

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                        Application                               │
├─────────────────────────────────────────────────────────────────┤
│  Raster API (2D)              │  Direct API (per-string)        │
│  pb_raster_set_pixel(x,y)     │  pb_set_pixel(string, pixel)    │
│  pb_raster_fill()             │  pb_clear_board()               │
│  pb_raster_show()             │  pb_show()                      │
├─────────────────────────────────────────────────────────────────┤
│                    Bit-Plane Encoding                           │
│  Colors → pb_value_bits_t[pixel][channel].planes[8]             │
├─────────────────────────────────────────────────────────────────┤
│                    Double Buffer                                 │
│  Back buffer (CPU writes) ←→ Front buffer (DMA reads)           │
├─────────────────────────────────────────────────────────────────┤
│  DMA Channel → PIO TX FIFO → 32 GPIOs (parallel)                │
└─────────────────────────────────────────────────────────────────┘
```

## Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| PIO Strategy | Single SM, 32 pins | Perfect sync across all strings |
| Buffering | Double-buffered | DMA safety, no tearing |
| Memory | Static allocation | No heap, predictable |
| Color Storage | Bit-plane encoded | Enables parallel DMA output |
| FPS Limiting | Hybrid sleep/spin | Power-efficient + precise timing |

## Bit-Plane Encoding

The driver uses bit-plane encoding to enable parallel output of all 32 strings simultaneously.

For each pixel position and color channel, data is stored as:

```c
typedef struct {
    uint32_t planes[8];  // planes[0]=MSB, planes[7]=LSB
} pb_value_bits_t;
```

Each `planes[i]` contains 32 bits - one bit per string. This allows the PIO to output one bit-plane (all 32 strings) per 32-bit DMA word.

**Example**: Setting string 5, pixel 0 to red (0xFF0000):
- For the red channel at pixel 0, all 8 bit planes get bit 5 set to 1
- Green and blue channels get bit 5 set to 0

## File Structure

| File | Purpose |
|------|---------|
| `pb_led_driver.h` | Public API and types |
| `pb_led_driver.c` | Core logic: init, pixel encoding, show |
| `pb_led_driver_hw.c` | PIO/DMA hardware (RP2350 only) |
| `pb_led_driver_raster.c` | 2D raster abstraction with mapping |
| `pb_led_driver_color.c` | HSV/RGB conversion, blending |
| `ws2811_parallel.pio` | PIO assembly for WS2811 timing |

## API Reference

### Driver Lifecycle

```c
// Initialize - returns NULL on failure
pb_driver_t* pb_driver_init(const pb_driver_config_t* config);

// Cleanup
void pb_driver_deinit(pb_driver_t* driver);

// Access config
const pb_driver_config_t* pb_driver_get_config(const pb_driver_t* driver);
```

### Direct Pixel Access

```c
// Set pixel (board is typically 0 for single-board)
void pb_set_pixel(pb_driver_t* driver, uint8_t board, uint8_t string,
                  uint16_t pixel, pb_color_t color);

// Get pixel value
pb_color_t pb_get_pixel(const pb_driver_t* driver, uint8_t board,
                        uint8_t string, uint16_t pixel);

// Clear operations
void pb_clear_board(pb_driver_t* driver, uint8_t board, pb_color_t color);
void pb_clear_all(pb_driver_t* driver, pb_color_t color);
```

### Display Output

```c
// Blocking - waits for DMA complete + reset delay
void pb_show(pb_driver_t* driver);

// Non-blocking - returns immediately, may skip if busy
void pb_show_async(pb_driver_t* driver);

// Wait for async to complete
void pb_show_wait(pb_driver_t* driver);

// Check if busy
bool pb_show_busy(const pb_driver_t* driver);

// Frame-rate limited (hybrid sleep/spin for precision)
void pb_show_with_fps(pb_driver_t* driver, uint16_t target_fps);
```

### Statistics

```c
uint16_t pb_get_fps(const pb_driver_t* driver);      // 1-second averaged
uint32_t pb_get_frame_count(const pb_driver_t* driver);
```

### Raster Abstraction

The raster layer provides a 2D coordinate system with precomputed mapping to physical LEDs.

```c
// Create raster
pb_raster_config_t cfg = {
    .width = 50,
    .height = 32,
    .board = 0,
    .start_string = 0,
    .start_pixel = 0,
    .wrap_mode = PB_WRAP_CLIP,  // Each row = one string
};
int id = pb_raster_create(driver, &cfg);
pb_raster_t* raster = pb_raster_get(driver, id);

// Draw
pb_raster_set_pixel(raster, x, y, color);
pb_raster_fill(raster, color);

// Copy to LED buffer and display
pb_raster_show(driver, raster);
pb_show(driver);
```

## Wrap Modes Explained

Wrap modes control how 2D raster coordinates (x, y) map to physical LED positions (string, pixel). Choose based on your physical LED layout.

### PB_WRAP_CLIP (Default)

**Use case**: Multiple independent LED strings, each driven as a separate row.

Each row Y maps to string Y. Pixel X maps directly to pixel position. This is the simplest mapping where each string is a horizontal row.

```
Raster (5x3):              Physical Layout:
                           String 0: [0][1][2][3][4]
  y=0: [0,0][1,0][2,0]...  String 1: [0][1][2][3][4]
  y=1: [0,1][1,1][2,1]...  String 2: [0][1][2][3][4]
  y=2: [0,2][1,2][2,2]...

  (x,y) → string=y, pixel=x
```

```c
pb_raster_config_t cfg = {
    .width = 50,
    .height = 32,
    .wrap_mode = PB_WRAP_CLIP,
};
// Result: 32 strings, 50 pixels each
// raster(10, 5) → string 5, pixel 10
```

### PB_WRAP_ZIGZAG

**Use case**: Folded/serpentine LED strips where alternating rows run in opposite directions.

Common when a single long strip is folded back and forth to create a matrix. Odd rows are reversed.

```
Physical wiring (single strip folded):

  String 0: [0]→[1]→[2]→[3]→[4]
                              ↓
  String 1: [9]←[8]←[7]←[6]←[5]
            ↓
  String 2: [10]→[11]→[12]→[13]→[14]

Raster mapping:
  y=0: pixels 0-4  (left to right)
  y=1: pixels 9-5  (right to left, reversed!)
  y=2: pixels 10-14 (left to right)

  raster(0,0)=pixel 0,  raster(4,0)=pixel 4
  raster(0,1)=pixel 9,  raster(4,1)=pixel 5  ← reversed
  raster(0,2)=pixel 10, raster(4,2)=pixel 14
```

```c
pb_raster_config_t cfg = {
    .width = 5,
    .height = 3,
    .wrap_mode = PB_WRAP_ZIGZAG,
};
// Handles the serpentine reversal automatically
```

### PB_WRAP_CHAIN

**Use case**: Multiple physical strings chained together to form longer logical rows.

When your display width exceeds max_pixel_length, chain multiple strings end-to-end.

```
Example: 100-pixel wide display, but strings are 50 pixels max

Physical strings:
  String 0: [pixels 0-49]   ← first half of row 0
  String 1: [pixels 0-49]   ← second half of row 0
  String 2: [pixels 0-49]   ← first half of row 1
  String 3: [pixels 0-49]   ← second half of row 1

Raster (100x2):
  y=0: x=0-49  → string 0, pixel 0-49
       x=50-99 → string 1, pixel 0-49
  y=1: x=0-49  → string 2, pixel 0-49
       x=50-99 → string 3, pixel 0-49
```

```c
pb_raster_config_t cfg = {
    .width = 100,
    .height = 2,
    .wrap_mode = PB_WRAP_CHAIN,
    .chain_length = 50,  // pixels per physical string
};
// raster(75, 0) → string 1, pixel 25
// raster(75, 1) → string 3, pixel 25
```

**Note**: Width must be evenly divisible by chain_length.

### PB_WRAP_NONE

**Use case**: Raw sequential mapping with no special handling.

Pixels are mapped linearly across all strings. When one string fills, continues to the next.

```
Raster (8x2) with 5 pixels per string:

  Linear index: 0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15
                └──string 0──┘  └──string 1──┘  └──string 2──┘  └─string 3─┘

  raster(0,0)=idx 0  → string 0, pixel 0
  raster(4,0)=idx 4  → string 0, pixel 4
  raster(5,0)=idx 5  → string 1, pixel 0  ← wrapped to next string
  raster(0,1)=idx 8  → string 1, pixel 3
```

```c
pb_raster_config_t cfg = {
    .width = 8,
    .height = 2,
    .wrap_mode = PB_WRAP_NONE,
};
// Continuous linear mapping across strings
```

### Choosing the Right Wrap Mode

| Layout | Wrap Mode | Notes |
|--------|-----------|-------|
| Parallel strings (standard matrix) | `PB_WRAP_CLIP` | Row Y = String Y |
| Folded single strip | `PB_WRAP_ZIGZAG` | Handles direction reversal |
| Wide display from short strings | `PB_WRAP_CHAIN` | Concatenates strings |
| Custom/raw access | `PB_WRAP_NONE` | Linear sequential |

### Color Utilities

```c
pb_color_t pb_color_rgb(uint8_t r, uint8_t g, uint8_t b);
pb_color_t pb_color_hsv(uint8_t h, uint8_t s, uint8_t v);  // h: 0-255
pb_color_t pb_color_scale(pb_color_t color, uint8_t scale);
pb_color_t pb_color_blend(pb_color_t c1, pb_color_t c2, uint8_t amount);
```

## Configuration Options

### pb_driver_config_t

| Field | Type | Description |
|-------|------|-------------|
| `board_id` | uint8_t | This board's ID (0 = main) |
| `num_boards` | uint8_t | Total boards (typically 1) |
| `gpio_base` | uint8_t | First GPIO pin (typically 0) |
| `num_strings` | uint8_t | Number of strings (1-32) |
| `strings[]` | pb_string_config_t | Per-string length/enabled |
| `max_pixel_length` | uint16_t | Maximum pixels in any string |
| `frequency_hz` | uint32_t | Bit frequency (800000 typical) |
| `color_order` | pb_color_order_t | LED color order |
| `reset_us` | uint16_t | Reset time (200 typical) |
| `pio_index` | uint8_t | Which PIO (0 or 1) |

### Color Orders

Different LED types expect different color orderings:
- `PB_COLOR_ORDER_GRB` - WS2812
- `PB_COLOR_ORDER_RGB` - Some WS2811
- `PB_COLOR_ORDER_BRG` - Other WS2811 variants

**Tip**: If colors look wrong, try different color orders until RGB displays correctly.

## Compile-Time Limits

Defined in `pb_led_driver.h`, can be overridden:

```c
#define PB_MAX_BOARDS 4        // Max boards supported
#define PB_MAX_PIXELS 256      // Max pixels per string
#define PB_MAX_STRINGS 32      // Max strings (fixed by PIO)
#define PB_MAX_RASTERS 16      // Max simultaneous rasters
```

Raster pixel pool size (default 8192 pixels):
```c
#define PB_RASTER_POOL_SIZE 8192
```

## Memory Usage

For 32 strings × 256 max pixels:
- Bit-plane buffers: 2 × 256 × 3 × 32 bytes = 48KB
- Raster pool: 8192 × 8 bytes = 64KB (if using rasters)
- Driver struct: ~1KB

## Multicore Usage

For stable frame rates when the main core is busy (e.g., display updates), run the LED output loop on core1:

```c
// main.c
volatile bool led_running = false;

void core1_led_entry(void) {
    while (led_running) {
        update_pixels();
        pb_show_with_fps(driver, 120);
    }
}

// Start on core1
led_running = true;
multicore_launch_core1(core1_led_entry);

// Stop
led_running = false;
multicore_reset_core1();
```

## PIO Program Details

The `ws2811_parallel.pio` program outputs all 32 strings simultaneously with WS2811 timing:

```
Bit timing (800kHz, 1.25µs per bit):
  T1: 375ns HIGH (start pulse)
  T2: 375ns DATA (0=LOW, 1=HIGH)
  T3: 500ns LOW (end pulse)
```

Each PIO cycle:
1. `mov pins, !null` - All pins HIGH (T1)
2. `out pins, 32` - Output data bits (T2)
3. `mov pins, null` - All pins LOW (T3)

Autopull refills OSR from TX FIFO. DMA feeds the FIFO continuously.

## Test Build

Define `PB_LED_DRIVER_TEST_BUILD` for host testing. This stubs out hardware functions and allows unit testing the bit-plane encoding and raster mapping logic.

```bash
gcc -DPB_LED_DRIVER_TEST_BUILD -o test test_pb_led_driver.c pb_led_driver.c pb_led_driver_raster.c pb_led_driver_color.c
./test
```

## Troubleshooting

**Wrong colors**: Try different `color_order` settings

**Flickering**: Ensure DMA completes before modifying back buffer. Use `pb_show()` (blocking) or check `pb_show_busy()`.

**Low FPS**:
- Check for blocking I/O on same core
- Use multicore for LED output
- Verify `max_pixel_length` isn't oversized

**First pixel wrong**: Normal WS2811 behavior - first pixel sometimes latches incorrectly. Some setups add a sacrificial pixel.

## License

Part of pixel_blit_firmware project.
