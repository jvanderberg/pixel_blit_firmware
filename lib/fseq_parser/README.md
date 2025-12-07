# FSEQ Parser Library

## Overview

`lib/fseq_parser` is a lightweight, streaming parser for **xLights FSEQ v2** sequence files. It is designed for memory-constrained embedded systems like the RP2350.

## Key Features

*   **Zero Copy / Push Architecture:** You feed it chunks of data (any size) as you read them from storage. It processes them byte-by-byte without requiring large internal buffers.
*   **Streaming State:** Handles pixels that straddle buffer boundaries (e.g., Red byte at the end of Chunk A, Green/Blue bytes at the start of Chunk B).
*   **Hardware Agnostic:** Decoupled from the LED driver. It invokes a user-provided callback `(String, Pixel, Color)` for every completed pixel.
*   **Linear Mapping:** Assumes standard "One File Per Board" or "Master File" layout where channels map linearly to strings (String 0 first, then String 1, etc.).

## Usage

### 1. Initialization

Initialize the parser with your hardware layout and a pixel callback.

```c
// Define your layout
fseq_layout_t layout = {
    .num_strings = 32,
    .pixels_per_string = 50
};

// Create parser context
fseq_parser_ctx_t* ctx = fseq_parser_init(user_data, my_pixel_callback, layout);
```

### 2. Reading Header

Read the first 32 bytes of the file to validate format and get frame timing.

```c
uint8_t header_buf[32];
// ... read 32 bytes from file ...

fseq_header_t header;
if (fseq_parser_read_header(ctx, header_buf, &header)) {
    printf("FPS: %d, Frames: %d\n", 1000 / header.step_time_ms, header.frame_count);
}
```

### 3. Streaming Data (Main Loop)

Read data from your source (SD card, Network) in efficient chunks (e.g., 512 bytes) and push it into the parser.

```c
while (playing) {
    // Read from SD card
    uint8_t buffer[512];
    uint32_t bytes_read = f_read(file, buffer, 512, ...);
    
    // Push to parser
    bool frame_done = fseq_parser_push(ctx, buffer, bytes_read);
    
    if (frame_done) {
        // A full frame has been parsed and sent to the pixel callback.
        // Trigger your hardware show() function here.
    }
}
```

### 4. The Pixel Callback

This function is called by the parser whenever a complete RGB triplet is assembled.

```c
void my_pixel_callback(void* user_data, uint8_t string, uint16_t pixel, uint32_t color) {
    // Directly write to your LED driver's back buffer
    pb_set_pixel(driver, string, pixel, color);
}
```

## File Format Support

*   **Supported:** FSEQ v2.0 Uncompressed.
*   **Unsupported:** Compression (ZSTD/ZLIB), Sparse Ranges (will be treated as linear gaps).

## Architecture

```
[SD Card] --> [Buffer (512b)] --> [fseq_parser_push] 
                                        |
                                        v
                                  [State Machine]
                                        |
                                        v
                                  [pixel_callback] --> [pb_led_driver]
```
