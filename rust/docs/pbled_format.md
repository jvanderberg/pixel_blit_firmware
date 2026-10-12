# PBLED Format Specification

## Overview

PBLED (Pixel Blit LED) is a compressed LED sequence format optimized for embedded playback. It addresses performance limitations of FSEQ by using:

- **Palettized colors**: 1-8 bits per pixel instead of 24 bits
- **Frame RLE**: Repeat tokens for duplicate frames
- **Chunk-based encoding**: Mix encoding modes per-scene
- **Small file sizes**: Enables storage in flash/SRAM instead of SD card

## Motivation

Current FSEQ limitations on embedded systems:
- 6-7ms SD card read per frame limits FPS
- DMA contention between SD and LED output causes 30-60ms stalls
- Large files (megabytes) require SD card streaming

PBLED goals:
- 10-100x smaller files for typical shows
- Files small enough to fit in 16MB flash or SRAM
- Fast enough decoding to eliminate SD card as bottleneck

## File Structure

```
+------------------+
| File Header      |
+------------------+
| Chunk 0          |
+------------------+
| Chunk 1          |
+------------------+
| ...              |
+------------------+
| Chunk N          |
+------------------+
```

## File Header

```rust
struct PbledHeader {
    magic: [u8; 4],      // "PBLE"
    version: u8,         // Format version (1)
    flags: u8,           // Reserved
    frame_count: u32,    // Total frames after expansion
    channel_count: u32,  // Total channels (pixels * 3)
    fps: u16,            // Target playback FPS
    string_count: u8,    // Number of LED strings
    reserved: [u8; 13],  // Pad to 32 bytes
}
```

## Chunk Format

Each chunk has a header followed by type-specific data:

```rust
struct ChunkHeader {
    chunk_type: u8,
    length: u16,   // Bytes of data following header
}
```

### Chunk Types

| Type | Value | Description |
|------|-------|-------------|
| PALETTE | 0x01 | Define new color palette |
| FRAME | 0x02 | Palettized frame data |
| REPEAT | 0x03 | Repeat previous frame(s) |
| RAW | 0x04 | Raw RGB data (fallback) |
| SEGMENT_DEF | 0x10 | Define a reusable segment |
| SEGMENT_REF | 0x11 | Reference a defined segment |

## Chunk Details

### PALETTE (0x01)

Sets the active palette for subsequent FRAME chunks.

```rust
struct PaletteChunk {
    color_count: u8,           // 1-255 colors
    colors: [[u8; 3]; count],  // RGB values
}
```

Bit depth is calculated as `ceil(log2(color_count))`:
- 2 colors = 1 bit/pixel
- 3-4 colors = 2 bits/pixel
- 5-8 colors = 3 bits/pixel
- 9-16 colors = 4 bits/pixel
- 17-256 colors = 8 bits/pixel

### FRAME (0x02)

One frame of palettized pixel data.

```rust
struct FrameChunk {
    data: [u8],  // Packed palette indices at current bit depth
}
```

Pixels are packed LSB-first. For 3-bit depth:
- Byte 0: pixels 0-1 (bits 0-2, 3-5) + 2 unused bits
- And so on...

### REPEAT (0x03)

Repeat the previous frame N times.

```rust
struct RepeatChunk {
    count: u16,  // Number of times to repeat (1-65535)
}
```

### RAW (0x04)

Uncompressed RGB data for frames that don't palettize well.

```rust
struct RawChunk {
    data: [u8],  // RGB triplets, 3 bytes per pixel
}
```

### SEGMENT_DEF (0x10)

Define a named segment for later reference.

```rust
struct SegmentDefChunk {
    segment_id: u8,
    frame_count: u16,
    // Followed by PALETTE/FRAME/REPEAT chunks until frame_count reached
}
```

### SEGMENT_REF (0x11)

Reference a previously defined segment.

```rust
struct SegmentRefChunk {
    segment_id: u8,
    repeat_count: u16,  // Play segment this many times
}
```

## Compression Examples

### Example 1: Simple 2-color chase

Original FSEQ: 3000 frames, 1000 pixels = 9MB

PBLED:
- PALETTE: 2 colors (6 bytes)
- FRAME: 1000 bits = 125 bytes per frame
- Frames with RLE: ~500 unique frames
- **Total: ~63KB (140x compression)**

### Example 2: 5fps show with 1-second holds

Original FSEQ: 300 frames at 5fps, 1000 pixels = 900KB

PBLED (assuming 5 colors, each frame holds for 5 frames):
- PALETTE: 5 colors (15 bytes)
- 60 unique frames at 375 bytes each = 22.5KB
- REPEAT chunks for holds
- **Total: ~25KB (36x compression)**

### Example 3: Looping show

Original FSEQ: 100-frame loop repeated 10x = 1000 frames = 3MB

PBLED:
- SEGMENT_DEF: 100 frames
- SEGMENT_REF: repeat 10x
- **Total: ~10KB for segment (300x compression)**

## Conversion Tool

Offline tool to convert FSEQ to PBLED:

1. **Color Analysis**
   - Scan all frames, collect unique colors
   - Group consecutive frames by color similarity
   - Threshold for new palette: >20% color change

2. **Frame Deduplication**
   - Hash each frame
   - Replace duplicates with REPEAT

3. **Segment Detection**
   - Identify repeating frame sequences
   - Create SEGMENT_DEF for sequences >10 frames repeated >2x

4. **Bit Depth Optimization**
   - Per-palette-group, use minimum bit depth
   - Consider quantization if slightly over threshold

5. **Output**
   - Emit chunks in playback order
   - Report compression ratio

## Decoder Implementation

```rust
struct PbledDecoder {
    palette: [[u8; 3]; 256],
    bit_depth: u8,
    segments: [Segment; 16],
    current_segment: Option<u8>,
}

impl PbledDecoder {
    fn decode_frame(&mut self, reader: &mut Reader, buffer: &mut FrameBuffer) -> bool {
        let chunk_type = reader.read_u8();
        let length = reader.read_u16();

        match chunk_type {
            PALETTE => self.load_palette(reader, length),
            FRAME => self.decode_palettized(reader, buffer),
            REPEAT => self.repeat_count = reader.read_u16(),
            RAW => self.decode_raw(reader, buffer),
            SEGMENT_REF => self.start_segment(reader),
            _ => skip(reader, length),
        }
    }
}
```

## Storage Options

With typical 10-100x compression:

| Original Size | Compressed | Storage Option |
|--------------|------------|----------------|
| 1MB | 10-100KB | SRAM (520KB available) |
| 10MB | 100KB-1MB | Flash (15MB available) |
| 100MB+ | 1-10MB | Flash or SD streaming |

## Future Extensions

- **Delta frames**: Store only changed pixels
- **Spatial RLE**: Run-length within a frame
- **Keyframes**: Periodic full frames for seeking
- **Audio sync**: Timing markers for music synchronization

## Implementation Phases

### Phase 1: Basic Decoder
- File header parsing
- PALETTE and FRAME chunks
- REPEAT chunks
- RAW fallback

### Phase 2: Converter Tool
- FSEQ parsing
- Color analysis and palette generation
- Frame hashing and RLE
- Output PBLED format

### Phase 3: Advanced Features
- Segment definitions and references
- Flash storage integration
- SD-to-flash caching

### Phase 4: Optimization
- Bit-packed pixel extraction (SIMD-like)
- DMA-friendly alignment
- Streaming decoder for large files
