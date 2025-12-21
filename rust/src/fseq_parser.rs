//! Streaming parser for xLights FSEQ v2 sequence files.
//!
//! Port of the C fseq_parser library.
//!
//! # Usage
//!
//! ```ignore
//! let mut parser = FseqParser::new(layout);
//! let header = parser.parse_header(&header_bytes)?;
//!
//! // Push chunks of channel data
//! while let Some(chunk) = read_chunk() {
//!     if parser.push(&chunk) {
//!         // Frame complete! Read pixels from parser
//!         for (string, pixel, color) in parser.pixels() {
//!             frame.set_pixel(string, pixel, color);
//!         }
//!         break;
//!     }
//! }
//! ```

use crate::led_driver::Rgb;
use crate::sdcard::MAX_STRINGS;

/// Maximum pixels per string for layout
pub const MAX_PIXELS_PER_STRING: u16 = 512;

/// FSEQ v2 Header (32 bytes)
///
/// Note: Not using `#[repr(packed)]` to avoid alignment issues.
/// We parse fields manually from the byte buffer instead.
#[derive(Clone, Copy, Debug, Default)]
pub struct FseqHeader {
    /// Magic number ('PSEQ' = 0x51455350)
    pub magic: u32,
    /// Offset to channel data
    pub channel_data_offset: u16,
    /// Minor version
    pub minor_version: u8,
    /// Major version (should be 2)
    pub major_version: u8,
    /// Header length
    pub header_length: u16,
    /// Total channels per frame
    pub channel_count: u32,
    /// Total frames in file
    pub frame_count: u32,
    /// Frame delay in milliseconds (e.g., 20ms = 50fps)
    pub step_time_ms: u8,
    /// Flags
    pub flags: u8,
    /// Compression type (0 = uncompressed)
    pub compression_type: u8,
    /// Number of compression blocks (lower 4 bits)
    pub num_compression_blocks: u8,
    /// Number of compression blocks (high byte)
    pub num_compression_blocks_high: u8,
    /// Number of sparse ranges
    pub num_sparse_ranges: u8,
    /// Reserved
    pub reserved: u8,
    /// Unique ID
    pub unique_id: u64,
}

impl FseqHeader {
    /// Parse header from 32-byte buffer
    pub fn parse(buffer: &[u8; 32]) -> Option<Self> {
        // Check magic ('PSEQ')
        let magic = u32::from_le_bytes([buffer[0], buffer[1], buffer[2], buffer[3]]);
        if magic != 0x51455350 {
            return None;
        }

        // Check version
        let major_version = buffer[7];
        if major_version != 2 {
            return None;
        }

        Some(Self {
            magic,
            channel_data_offset: u16::from_le_bytes([buffer[4], buffer[5]]),
            minor_version: buffer[6],
            major_version,
            header_length: u16::from_le_bytes([buffer[8], buffer[9]]),
            channel_count: u32::from_le_bytes([buffer[10], buffer[11], buffer[12], buffer[13]]),
            frame_count: u32::from_le_bytes([buffer[14], buffer[15], buffer[16], buffer[17]]),
            step_time_ms: buffer[18],
            flags: buffer[19],
            compression_type: buffer[20],
            num_compression_blocks: buffer[21],
            num_compression_blocks_high: buffer[22],
            num_sparse_ranges: buffer[23],
            reserved: buffer[24],
            unique_id: u64::from_le_bytes([
                buffer[25], buffer[26], buffer[27], buffer[28],
                buffer[29], buffer[30], buffer[31], 0,
            ]),
        })
    }

    /// Get target FPS from step time
    pub fn fps(&self) -> u16 {
        if self.step_time_ms > 0 {
            1000 / self.step_time_ms as u16
        } else {
            30
        }
    }
}

/// Layout configuration for address mapping
#[derive(Clone)]
pub struct FseqLayout {
    /// Number of strings
    pub num_strings: u8,
    /// Pixel count per string
    pub string_lengths: [u16; MAX_STRINGS],
}

impl Default for FseqLayout {
    fn default() -> Self {
        Self {
            num_strings: MAX_STRINGS as u8,
            string_lengths: [50; MAX_STRINGS],
        }
    }
}

/// A parsed pixel from the FSEQ stream
#[derive(Clone, Copy, Debug)]
pub struct FseqPixel {
    pub string: u8,
    pub pixel: u16,
    pub color: Rgb,
}

/// Streaming FSEQ parser
///
/// Processes channel data in chunks and fires pixel callbacks.
pub struct FseqParser {
    /// Layout configuration
    layout: FseqLayout,
    /// Cached header info
    header: FseqHeader,
    /// Current channel offset within frame
    current_channel_index: u32,
    /// Current string index
    current_string_idx: u8,
    /// Current pixel index within string
    current_pixel_idx: u16,
    /// Temporary buffer for assembling split pixels (R, G, B)
    temp_pixel: [u8; 3],
    /// Number of bytes in temp_pixel (0, 1, 2)
    temp_pixel_idx: u8,
    /// Accumulated pixels from current chunk (512 bytes / 3 = ~170 pixels max)
    pixels: heapless::Vec<FseqPixel, 256>,
    /// Overflow buffer for bytes that arrive after frame boundary
    overflow_buf: [u8; 512],
    /// Number of valid bytes in overflow buffer
    overflow_len: u16,
}

impl FseqParser {
    /// Create a new parser with the given layout
    pub fn new(layout: FseqLayout) -> Self {
        Self {
            layout,
            header: FseqHeader::default(),
            current_channel_index: 0,
            current_string_idx: 0,
            current_pixel_idx: 0,
            temp_pixel: [0; 3],
            temp_pixel_idx: 0,
            pixels: heapless::Vec::new(),
            overflow_buf: [0; 512],
            overflow_len: 0,
        }
    }

    /// Parse and validate the header
    pub fn parse_header(&mut self, buffer: &[u8; 32]) -> Option<FseqHeader> {
        let header = FseqHeader::parse(buffer)?;

        // Only support uncompressed for now
        if header.compression_type != 0 {
            return None;
        }

        self.header = header;
        Some(header)
    }

    /// Get the parsed header
    pub fn header(&self) -> &FseqHeader {
        &self.header
    }

    /// Reset parser state for a new file or loop
    pub fn reset(&mut self) {
        self.current_channel_index = 0;
        self.current_string_idx = 0;
        self.current_pixel_idx = 0;
        self.temp_pixel_idx = 0;
        self.pixels.clear();
        self.overflow_len = 0;
    }

    /// Process a single byte, returns true if frame boundary reached
    fn process_byte(&mut self, byte: u8) -> bool {
        // Accumulate bytes into temp_pixel
        self.temp_pixel[self.temp_pixel_idx as usize] = byte;
        self.temp_pixel_idx += 1;

        // If we have 3 bytes, we have a full pixel (R, G, B)
        if self.temp_pixel_idx == 3 {
            // Form RGB color (assumes RGB order in file - xLights String Type = RGB)
            let color = Rgb::new(
                self.temp_pixel[0],
                self.temp_pixel[1],
                self.temp_pixel[2],
            );

            // Output if within valid string bounds
            if self.current_string_idx < self.layout.num_strings {
                // Store pixel (don't overflow the buffer)
                let _ = self.pixels.push(FseqPixel {
                    string: self.current_string_idx,
                    pixel: self.current_pixel_idx,
                    color,
                });
            }

            // Advance pixel/string counters
            self.current_pixel_idx += 1;

            // Check if we reached end of current string
            if self.current_string_idx < self.layout.num_strings {
                let string_len = self.layout.string_lengths[self.current_string_idx as usize];
                if self.current_pixel_idx >= string_len {
                    self.current_string_idx += 1;
                    self.current_pixel_idx = 0;
                }
            }

            // Advance global state
            self.temp_pixel_idx = 0;
            self.current_channel_index += 3;

            // Frame boundary check
            if self.header.channel_count > 0 && self.current_channel_index >= self.header.channel_count {
                // Reset counters for next frame
                self.current_channel_index = 0;
                self.current_string_idx = 0;
                self.current_pixel_idx = 0;
                self.temp_pixel_idx = 0;
                return true; // Frame complete
            }
        }
        false
    }

    /// Push a chunk of data into the parser.
    ///
    /// Returns true if a frame was completed during this push.
    /// Call `take_pixels()` to get the frame's pixel data.
    pub fn push(&mut self, data: &[u8]) -> bool {
        // First, process any overflow bytes from previous call
        if self.overflow_len > 0 {
            let overflow_len = self.overflow_len as usize;
            for i in 0..overflow_len {
                if self.process_byte(self.overflow_buf[i]) {
                    // Frame completed while processing overflow
                    let remaining = overflow_len - i - 1;
                    if remaining > 0 {
                        // Move remaining overflow to start
                        for j in 0..remaining {
                            self.overflow_buf[j] = self.overflow_buf[i + 1 + j];
                        }
                    }
                    self.overflow_len = remaining as u16;
                    return true;
                }
            }
            self.overflow_len = 0;
        }

        // Process new data
        for (i, &byte) in data.iter().enumerate() {
            if self.process_byte(byte) {
                // Frame complete - save remaining bytes as overflow
                let remaining = data.len() - i - 1;
                if remaining > 0 && remaining <= self.overflow_buf.len() {
                    self.overflow_buf[..remaining].copy_from_slice(&data[i + 1..]);
                    self.overflow_len = remaining as u16;
                }
                return true;
            }
        }

        false
    }

    /// Get the accumulated pixels for the completed frame as a slice.
    pub fn pixels(&self) -> &[FseqPixel] {
        &self.pixels
    }

    /// Clear the pixel buffer for the next frame.
    pub fn clear_pixels(&mut self) {
        self.pixels.clear();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn make_header_bytes(channel_count: u32, frame_count: u32, step_time_ms: u8) -> [u8; 32] {
        let mut buf = [0u8; 32];
        // Magic 'PSEQ'
        buf[0..4].copy_from_slice(&0x51455350u32.to_le_bytes());
        // Channel data offset
        buf[4..6].copy_from_slice(&32u16.to_le_bytes());
        // Version 2.0
        buf[6] = 0; // minor
        buf[7] = 2; // major
        // Header length
        buf[8..10].copy_from_slice(&32u16.to_le_bytes());
        // Channel count
        buf[10..14].copy_from_slice(&channel_count.to_le_bytes());
        // Frame count
        buf[14..18].copy_from_slice(&frame_count.to_le_bytes());
        // Step time
        buf[18] = step_time_ms;
        // Rest is 0 (uncompressed, no sparse ranges)
        buf
    }

    #[test]
    fn test_header_parse_valid() {
        let buf = make_header_bytes(150, 100, 20);
        let header = FseqHeader::parse(&buf).unwrap();

        // Copy fields to avoid alignment issues in asserts
        let magic = header.magic;
        let major_version = header.major_version;
        let channel_count = header.channel_count;
        let frame_count = header.frame_count;
        let step_time_ms = header.step_time_ms;

        assert_eq!(magic, 0x51455350);
        assert_eq!(major_version, 2);
        assert_eq!(channel_count, 150);
        assert_eq!(frame_count, 100);
        assert_eq!(step_time_ms, 20);
        assert_eq!(header.fps(), 50);
    }

    #[test]
    fn test_header_parse_invalid_magic() {
        let mut buf = make_header_bytes(150, 100, 20);
        buf[0] = 0; // Break magic
        assert!(FseqHeader::parse(&buf).is_none());
    }

    #[test]
    fn test_header_parse_invalid_version() {
        let mut buf = make_header_bytes(150, 100, 20);
        buf[7] = 1; // Version 1
        assert!(FseqHeader::parse(&buf).is_none());
    }

    #[test]
    fn test_parser_single_pixel() {
        let layout = FseqLayout {
            num_strings: 1,
            string_lengths: {
                let mut arr = [0u16; MAX_STRINGS];
                arr[0] = 1;
                arr
            },
        };
        let mut parser = FseqParser::new(layout);

        // Parse header with 3 channels (1 pixel)
        let header_buf = make_header_bytes(3, 1, 20);
        parser.parse_header(&header_buf).unwrap();

        // Push RGB data
        let data = [255, 0, 128]; // R=255, G=0, B=128
        let frame_complete = parser.push(&data);

        assert!(frame_complete);
        let pixels = parser.pixels();
        assert_eq!(pixels.len(), 1);
        assert_eq!(pixels[0].string, 0);
        assert_eq!(pixels[0].pixel, 0);
        assert_eq!(pixels[0].color.r, 255);
        assert_eq!(pixels[0].color.g, 0);
        assert_eq!(pixels[0].color.b, 128);
    }

    #[test]
    fn test_parser_multiple_pixels() {
        let layout = FseqLayout {
            num_strings: 1,
            string_lengths: {
                let mut arr = [0u16; MAX_STRINGS];
                arr[0] = 2;
                arr
            },
        };
        let mut parser = FseqParser::new(layout);

        // 2 pixels = 6 channels
        let header_buf = make_header_bytes(6, 1, 20);
        parser.parse_header(&header_buf).unwrap();

        // Push 2 pixels
        let data = [255, 0, 0, 0, 255, 0]; // Red, Green
        let frame_complete = parser.push(&data);

        assert!(frame_complete);
        let pixels = parser.pixels();
        assert_eq!(pixels.len(), 2);
        assert_eq!(pixels[0].color.r, 255);
        assert_eq!(pixels[1].color.g, 255);
    }

    #[test]
    fn test_parser_split_across_chunks() {
        let layout = FseqLayout {
            num_strings: 1,
            string_lengths: {
                let mut arr = [0u16; MAX_STRINGS];
                arr[0] = 1;
                arr
            },
        };
        let mut parser = FseqParser::new(layout);

        // 1 pixel = 3 channels
        let header_buf = make_header_bytes(3, 1, 20);
        parser.parse_header(&header_buf).unwrap();

        // Push data in parts
        assert!(!parser.push(&[255]));      // Just R
        assert!(!parser.push(&[128]));      // Just G
        assert!(parser.push(&[64]));        // B - frame complete

        let pixels = parser.pixels();
        assert_eq!(pixels.len(), 1);
        assert_eq!(pixels[0].color.r, 255);
        assert_eq!(pixels[0].color.g, 128);
        assert_eq!(pixels[0].color.b, 64);
    }

    #[test]
    fn test_parser_multiple_strings() {
        let layout = FseqLayout {
            num_strings: 2,
            string_lengths: {
                let mut arr = [0u16; MAX_STRINGS];
                arr[0] = 1;
                arr[1] = 1;
                arr
            },
        };
        let mut parser = FseqParser::new(layout);

        // 2 strings * 1 pixel * 3 channels = 6 channels
        let header_buf = make_header_bytes(6, 1, 20);
        parser.parse_header(&header_buf).unwrap();

        let data = [255, 0, 0, 0, 0, 255]; // String 0 = Red, String 1 = Blue
        let frame_complete = parser.push(&data);

        assert!(frame_complete);
        let pixels = parser.pixels();
        assert_eq!(pixels.len(), 2);
        assert_eq!(pixels[0].string, 0);
        assert_eq!(pixels[0].color.r, 255);
        assert_eq!(pixels[1].string, 1);
        assert_eq!(pixels[1].color.b, 255);
    }

    #[test]
    fn test_parser_overflow_handling() {
        let layout = FseqLayout {
            num_strings: 1,
            string_lengths: {
                let mut arr = [0u16; MAX_STRINGS];
                arr[0] = 1;
                arr
            },
        };
        let mut parser = FseqParser::new(layout);

        // 1 pixel per frame = 3 channels
        let header_buf = make_header_bytes(3, 2, 20);
        parser.parse_header(&header_buf).unwrap();

        // Push two frames at once - should complete first frame and store overflow
        let data = [255, 0, 0, 0, 255, 0]; // Frame 1: Red, Frame 2: Green
        assert!(parser.push(&data)); // First frame complete

        {
            let pixels = parser.pixels();
            assert_eq!(pixels.len(), 1);
            assert_eq!(pixels[0].color.r, 255);
        }

        // Clear and process overflow for second frame
        parser.clear_pixels();
        assert!(parser.push(&[])); // Second frame from overflow

        let pixels = parser.pixels();
        assert_eq!(pixels.len(), 1);
        assert_eq!(pixels[0].color.g, 255);
    }

    #[test]
    fn test_parser_reset() {
        let layout = FseqLayout::default();
        let mut parser = FseqParser::new(layout);

        let header_buf = make_header_bytes(3, 1, 20);
        parser.parse_header(&header_buf).unwrap();

        // Partial push
        parser.push(&[255, 128]);
        assert_eq!(parser.temp_pixel_idx, 2);

        // Reset
        parser.reset();
        assert_eq!(parser.temp_pixel_idx, 0);
        assert_eq!(parser.current_channel_index, 0);
        assert!(parser.pixels().is_empty());
    }
}
