# High-Speed Parallel LED Driver Library (pb_led_driver)

## Overview

Standalone library for driving up to 32 WS2811/WS2812 LED strings in parallel using a single PIO state machine on RP2350B. Runtime-configurable with both direct string access and raster abstraction layers.

## Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Architecture | Standalone driver | Self-contained, no reactive pattern dependency |
| Configuration | Runtime init | Pass config struct, flexible deployment |
| PIO Strategy | Single SM, 32 pins | Perfect sync, simple DMA, minimal resources |
| Abstraction | Both layers | Direct `set_pixel()` + optional raster mapping |
| Buffering | Double-buffered, padded to max length | Safe DMA, strings ignore extra data |
| Storage | Bit-plane encoded | DMA-ready format, encode on write only |

## Multi-Board Architecture

The system supports multiple boards connected via LVDS (future):

- **Board 0 (Main)**: Holds buffers for ALL boards, outputs to local strings, sends remote board data over LVDS
- **Board N (Follower)**: Holds buffer for own strings only, receives data over LVDS

```
┌─────────────────────────────────────────────────────────┐
│ Board 0 (Main)                                          │
│  ┌──────────────────┐  ┌──────────────────┐            │
│  │ Board 0 Buffer   │  │ Board 1 Buffer   │  ...       │
│  │ (double-buffered)│  │ (double-buffered)│            │
│  └────────┬─────────┘  └────────┬─────────┘            │
│           │                     │                       │
│           ▼                     ▼                       │
│    Local PIO/DMA          Future: LVDS TX              │
│           │                     │                       │
│           ▼                     │                       │
│    32 LED Strings               │                       │
└─────────────────────────────────┼───────────────────────┘
                                  │
                    ┌─────────────┴─────────────┐
                    │ Board 1 (Follower)        │
                    │  ┌──────────────────┐     │
                    │  │ Board 1 Buffer   │     │
                    │  │ (double-buffered)│     │
                    │  └────────┬─────────┘     │
                    │           │               │
                    │           ▼               │
                    │    Local PIO/DMA          │
                    │           │               │
                    │           ▼               │
                    │    32 LED Strings         │
                    └───────────────────────────┘
```

## Architecture

### Bit-Plane Encoding (from cblinken)

Data is stored in DMA-ready bit-plane format. **Encoding happens once on `set_pixel()`, not on every frame.**

```
For each pixel position, 3 color channels (G,R,B), each encoded as:
  pb_value_bits_t {
    uint32_t planes[8];  // planes[0]=MSB, planes[7]=LSB
  }

Each planes[i] is 32 bits - one bit per string (bit 0 = string 0, etc.)
```

**Why bit-plane?**
- **25% smaller** than raw RGB (96 vs 128 bytes per pixel position)
- **DMA-ready**: No transformation needed at show time
- **Encode-on-write**: Only touched pixels cost CPU time
- **Static scenes = free**: Unchanged frames just re-DMA the same memory

### Memory Layout

```c
// Compile-time maximums
#define PB_MAX_BOARDS  4
#define PB_MAX_PIXELS  256

// Buffer allocation (board 0 holds all, followers hold only their own)
#if PB_BOARD_ID == 0
  static pb_value_bits_t buffers[PB_MAX_BOARDS][2][PB_MAX_PIXELS * 3];
#else
  static pb_value_bits_t buffers[1][2][PB_MAX_PIXELS * 3];
#endif
```

### Memory Estimates

Per pixel position (all 32 strings): **96 bytes**
- 3 channels × 8 planes × 4 bytes = 96 bytes
- Compare to raw RGB: 32 strings × 4 bytes = 128 bytes (33% larger)

| Configuration | Single-buffered | Double-buffered |
|---------------|-----------------|-----------------|
| 1 board × 256 pixels | 24 KB | 48 KB |
| 2 boards × 256 pixels | 48 KB | 96 KB |
| 4 boards × 256 pixels | 96 KB | 192 KB |
| 4 boards × 512 pixels | 192 KB | 384 KB |

RP2350B has 520KB SRAM - 4 boards × 256 pixels fits comfortably.

### DMA Chain
```
fragment_list[] → Chain DMA → Main DMA (8 words) → PIO TX FIFO → 32 GPIOs
                     ↑
            [ptr0, ptr1, ptr2, ..., NULL]
```

Board 0 outputs locally. Remote board data will be serialized to LVDS (future).

## Public API

### Configuration & Lifecycle
```c
pb_driver_config_t config = {
    .board_id = 0,                // This board's ID (0 = main)
    .num_boards = 4,              // Total boards in system (main only)
    .gpio_base = 0,
    .num_strings = 32,
    .strings = {{.length=50, .enabled=true}, ...},
    .max_pixel_length = 256,      // Compile-time max: PB_MAX_PIXELS
    .frequency_hz = 800000,
    .color_order = PB_COLOR_ORDER_GRB,
    .pio_index = 0,
};

pb_driver_t* driver = pb_driver_init(&config);
pb_driver_deinit(driver);
```

### Low-Level API (Direct String Access)
```c
// Board 0 can write to any board; followers only write to board 0 (themselves)
pb_set_pixel(driver, board, string, pixel, color);  // Encodes to bit-plane immediately
pb_get_pixel(driver, board, string, pixel);
pb_set_string(driver, board, string, colors_array);
pb_clear_board(driver, board, color);
pb_clear_all(driver, color);

pb_show(driver);              // Blocking - just triggers DMA, no encoding
pb_show_async(driver);        // Non-blocking
pb_show_with_fps(driver, 60); // Frame rate limited
```

### High-Level API (Raster Abstraction)

The raster layer provides a virtual 2D coordinate system that maps to physical LEDs.

```c
pb_raster_config_t cfg = {
    .board = 0,                   // Which board this raster maps to
    .width = 10, .height = 5,
    .start_string = 0, .start_pixel = 0,
    .wrap_mode = PB_WRAP_ZIGZAG,
};
int id = pb_raster_create(driver, &cfg);
pb_raster_t* r = pb_raster_get(driver, id);

pb_raster_set_pixel(r, x, y, color);
pb_raster_fill(r, color);
pb_raster_show(driver, r);                             // Encodes to board's bit-plane buffer
pb_raster_show_scrolled(driver, r, shift_x, shift_y);  // 16.16 fixed-point
```

#### Raster-to-Physical Mapping

At `pb_raster_create()` time, a mapping array is built based on the wrap mode:

```c
// Internal structure
struct pb_raster {
    pb_raster_config_t config;
    pb_color_t* pixels;              // [width * height] - raster pixel buffer
    struct {
        uint8_t  board;
        uint8_t  string;
        uint16_t pixel;
    }* pixel_map;                    // [width * height] - precomputed mapping
};
```

**Wrap Modes:**

```
PB_WRAP_CLIP - Each row is one string, clip at width
  Row 0: String 0, pixels 0-9
  Row 1: String 1, pixels 0-9
  ...

PB_WRAP_CONTINUOUS - Fill sequentially across strings
  Row 0: String 0, pixels 0-9
  Row 1: String 0, pixels 10-19 (or String 1 if overflow)
  ...

PB_WRAP_ZIGZAG - Serpentine for folded LED strips
  Row 0: String 0, pixels 0→9  (left to right)
  Row 1: String 0, pixels 19→10 (right to left)
  Row 2: String 0, pixels 20→29 (left to right)
  ...
```

**Usage flow:**
```c
pb_raster_set_pixel(raster, x, y, color) {
    // 1. Store in raster's pixel buffer (for scrolling/effects)
    raster->pixels[y * width + x] = color;
}

pb_raster_show(driver, raster) {
    // 2. Copy to bit-plane buffer using precomputed mapping
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            map = &raster->pixel_map[y * width + x];
            color = raster->pixels[y * width + x];
            pb_set_pixel(driver, map->board, map->string, map->pixel, color);
        }
    }
}
```

This separates concerns:
- **Raster**: Virtual canvas with (x, y) coordinates
- **Mapping**: Built once at create time, handles physical layout quirks
- **Low-level**: Encodes to DMA-ready bit-planes

### Utilities
```c
pb_color_rgb(r, g, b);
pb_color_hsv(h, s, v);
pb_color_blend(c1, c2, amount);
pb_get_fps(driver);
```

## File Structure

```
lib/pb_led_driver/
├── CMakeLists.txt
├── pb_led_driver.h           # Public API
├── pb_led_driver_internal.h  # Internal types
├── pb_led_driver.c           # Core: init, set_pixel, show
├── pb_led_driver_pio.c       # PIO setup
├── pb_led_driver_dma.c       # DMA chain setup
├── pb_led_driver_raster.c    # Raster abstraction
├── pb_led_driver_color.c     # Color utilities
└── ws2811_parallel.pio       # PIO program
```

## Implementation Steps

### 1. Create directory structure and CMakeLists.txt
- Create `lib/pb_led_driver/` directory
- Write CMakeLists.txt with PIO header generation

### 2. Define types and public header (`pb_led_driver.h`)
- Config structs: `pb_driver_config_t`, `pb_string_config_t`, `pb_raster_config_t`
- Enums: `pb_color_order_t`, `pb_wrap_mode_t`
- All public function declarations

### 3. Define internal structures (`pb_led_driver_internal.h`)
- `pb_value_bits_t` - bit-plane encoded pixel data
- `struct pb_driver` - driver state (buffers, DMA, PIO, semaphore)
- `struct pb_raster` - raster state (pixels, mapping)

### 4. Write PIO program (`ws2811_parallel.pio`)
- Simplified from cblinken (no board addressing)
- 32-bit parallel output, WS2811 timing

### 5. Implement core driver (`pb_led_driver.c`)
- `pb_driver_init()` - allocate, configure, init PIO/DMA
- `pb_driver_deinit()` - cleanup
- `pb_set_pixel()` - bit-plane encoding (adapt from cblinken `put_pixel`)
- `pb_show()` / `pb_show_async()` - buffer swap, DMA trigger

### 6. Implement PIO setup (`pb_led_driver_pio.c`)
- Load program, configure SM for 32-pin output
- Clock divider for 800kHz timing

### 7. Implement DMA setup (`pb_led_driver_dma.c`)
- Two-channel chain architecture
- Interrupt handler for frame completion
- Reset delay via alarm

### 8. Implement raster layer (`pb_led_driver_raster.c`)
- `pb_raster_create()` - build pixel mapping based on wrap mode
- `pb_raster_show()` - copy raster to LED buffer via mapping
- `pb_raster_show_scrolled()` - sub-pixel scrolling with interpolation

### 9. Implement color utilities (`pb_led_driver_color.c`)
- RGB/HSV conversion
- Color blending, scaling

### 10. Integration and testing
- Add library to main CMakeLists.txt
- Create test program

## Reference Files

| Purpose | File | Key Lines |
|---------|------|-----------|
| Bit-plane structure | `cblinken/lib/types.h` | 27-32 |
| Buffer layout | `cblinken/lib/defines.h` | 37-39 |
| DMA chain setup | `cblinken/lib/libpixelblit.c` | 151-202 |
| Pixel encoding | `cblinken/lib/utils.c` | 222-253 |
| PIO program | `cblinken/lib/libpixelblit.c` | 53-68 |
| Raster creation | `cblinken/lib/utils.c` | 47-189 |
| Sub-pixel scrolling | `cblinken/lib/utils.c` | 679-754 |

## Memory Estimate

Per pixel position (all 32 strings): 3 channels × 32 bytes = **96 bytes**

**Board 0 (Main) with 4 boards × 256 pixels:**
- Buffers: 4 boards × 2 × 256 × 3 × 32 bytes = 192 KB
- Fragment list: 768 × 8 = 6 KB
- Driver struct: ~1 KB
- **Total: ~200 KB** (38% of RP2350B's 520KB SRAM)

**Follower board × 256 pixels:**
- Buffers: 1 board × 2 × 256 × 3 × 32 bytes = 48 KB
- Fragment list: 768 × 8 = 6 KB
- Driver struct: ~1 KB
- **Total: ~55 KB** (11% of SRAM)

### Compile-Time Maximums
```c
#define PB_MAX_BOARDS   4    // Max boards in system
#define PB_MAX_PIXELS 256    // Max pixels per string
#define PB_MAX_STRINGS 32    // Fixed: 32 GPIOs
```

## Offline Playback (Planned: xLights .fseq Support)

The driver will be extended to support high-performance playback of pre-rendered sequences from an SD card, specifically the **xLights `.fseq` (v2)** format. This will allow complex animations designed on a PC to be played back on the microcontroller without heavy runtime calculation.

### Planned Pipeline (Data Plane / Core 1)

Playback will run as a streaming loop on Core 1:

1.  **Read:** Raw RGB bytes will be read from the SD card in 512-byte blocks (aligning with sectors for speed).
2.  **Encode:** The raw RGB data will be immediately encoded into the **Bit-Plane Buffer** using `pb_set_pixel()`. This step essentially "un-interleaves" the bytes into the DMA-ready format.
3.  **Show:** When a full frame is ready and the frame timer elapses, `pb_show()` will trigger the DMA transfer.

### File Format Strategy

To minimize runtime overhead, we will use a **"One File Per Board"** strategy (or a "Master File with Offset" strategy).

*   **Structure:** The `.fseq` file contains a header (FPS, Frame Count) followed by a flat stream of RGB data.
*   **Mapping:** xLights handles all complex mapping (Matrix, Tree, 3D shapes). The firmware will see a simple linear array of channels.
    *   `Channel 0-2` -> String 0, Pixel 0 (RGB)
    *   `Channel 3-5` -> String 0, Pixel 1 (RGB)
    *   ...
    *   `Channel N` -> String 1, Pixel 0 (RGB)

This makes the firmware "dumb" and efficient. It simply streams bytes from disk to GPIOs.

## Future: PSRAM Streaming for Large Displays

Currently, bit-plane buffers reside entirely in internal SRAM. This fundamentally limits total display size:

**The problem:**
- Each pixel position (across all 32 strings) requires 96 bytes in bit-plane format
- Double-buffered 512-position display = 96 KB
- RP2350 has 520 KB SRAM total, but firmware needs some too

**Real-world constraints:**
- **Multi-board displays:** 4 boards × 32 strings × 500 pixels = 64,000 pixels. Main board must buffer all boards' data before LVDS transmission.
- **Sparse configurations:** A display with a few 500-pixel strings but mostly short ones still needs buffers sized for the longest string, wasting SRAM.
- **Large installations:** 100k+ pixel displays are common in professional lighting. Current architecture cannot support these.

The RP2350 supports external QSPI PSRAM (up to 16 MB), which could hold entire multi-board frame buffers and stream them to the PIO/LVDS during output.

### Bandwidth Analysis

**LED output timing (per board):**
- WS2812 bit rate: 800 kHz (1.25 µs/bit)
- 24 bits/pixel = 30 µs per pixel position
- 512 positions = 15.36 ms frame time (~65 fps max)

**Data rate required per board:**
- 96 bytes/position × 512 positions × 65 fps = **3.2 MB/s sustained**

**Multi-board scenario (4 boards, main board streams all):**
- 4 × 3.2 MB/s = **12.8 MB/s** (still well within PSRAM capability)

**PSRAM capability:**
- QSPI PSRAM at 133 MHz in QPI mode: 50+ MB/s sequential read
- **Conclusion: Bandwidth is sufficient** even for 4-board displays at full frame rate.

### RP2350 XIP/QMI Architecture

The RP2350 makes this straightforward via its XIP (Execute-In-Place) subsystem:

**Memory-mapped PSRAM:**
- QMI supports 2 QSPI chip selects (CS0 = flash, CS1 = PSRAM)
- PSRAM appears directly in XIP address space (e.g., `0x11000000`)
- DMA can read directly from PSRAM addresses
- 16 KB XIP cache provides automatic caching with 8-byte lines

**Address aliases:**
| Address | Function |
|---------|----------|
| `0x10...` | Cached XIP access |
| `0x14...` | Uncached, no allocation |
| `0x1C...` | Uncached, no translation |

### Proposed Architecture: Static PSRAM Allocation

```
┌─────────────────┐      ┌─────────────┐      ┌──────────┐
│ PSRAM Buffer    │ DMA  │ PIO TX FIFO │ PIO  │ 32 GPIOs │
│ (memory-mapped) │─────►│             │─────►│          │
│ @ 0x11000000    │      │             │      │          │
└─────────────────┘      └─────────────┘      └──────────┘
        ▲
        │ XIP Cache (16KB)
        │ auto-caches recent reads
```

**Key insight:** No ring buffer needed. DMA reads directly from PSRAM via XIP. The cache handles burst efficiency automatically.

### Static Buffer Allocation

Use linker script to place buffers in PSRAM:

```c
// In linker script (.ld):
MEMORY {
    FLASH(rx)  : ORIGIN = 0x10000000, LENGTH = 4M
    PSRAM(rwx) : ORIGIN = 0x11000000, LENGTH = 8M
    RAM(rwx)   : ORIGIN = 0x20000000, LENGTH = 520K
}

SECTIONS {
    .psram_data : {
        *(.psram_data)
    } > PSRAM
}

// In C code:
__attribute__((section(".psram_data")))
static pb_value_bits_t psram_buffer[PB_MAX_PIXELS * 3 * 2 * PB_MAX_BOARDS];
```

### Streaming DMA for Large Transfers

For sequential reads without stalling other DMA channels, use the XIP streaming interface:

```c
// Program streaming read from PSRAM
xip_ctrl_hw->stream_addr = (uint32_t)&psram_buffer[frame_offset];
xip_ctrl_hw->stream_ctr = frame_word_count;

// DMA from streaming FIFO to PIO
channel_config_set_dreq(&cfg, DREQ_XIP_STREAM);
dma_channel_configure(chan, &cfg,
    &pio->txf[sm],      // Write to PIO FIFO
    XIP_AUX_BASE,       // Read from XIP stream FIFO
    frame_word_count,
    true);
```

This provides near-maximum QSPI throughput with minimal bus contention.

### Implementation Steps

1. **Configure QMI for PSRAM** on CS1 (clock speed, read/write commands)
2. **Update linker script** with PSRAM memory region
3. **Declare buffers in PSRAM** via section attribute
4. **Modify `pb_driver_init()`** to use PSRAM buffer address
5. **Update DMA setup** to use XIP streaming for output

### Trade-offs

| Aspect | Current (SRAM only) | PSRAM Streaming |
|--------|---------------------|-----------------|
| Total display size | ~16k pixels (limited by SRAM) | 100k+ pixels |
| Multi-board | Limited by main board SRAM | Full 4-board support |
| Sparse configs | Wastes SRAM on short strings | Efficient |
| Latency | Zero (DMA from SRAM) | Minimal (ring buffer) |
| Complexity | Simple | DMA chaining, IRQ handler |
| CPU overhead | None during output | IRQ servicing (~1%) |

### When to Use

- **SRAM mode:** Single board, uniform string lengths, ≤16k total pixels
- **PSRAM mode:** Multi-board displays, sparse configurations, or 100k+ pixel installations

This could be a compile-time or runtime configuration option via `pb_driver_config_t`.

