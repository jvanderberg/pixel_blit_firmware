//! Bit-plane buffer for parallel WS2812 output.
//!
//! Ported from pb_led_driver.c
//!
//! The buffer uses bit-plane encoding: each u32 word represents one bit
//! of data for all 32 strings simultaneously. Bit N in the word corresponds
//! to string N's output for that bit position.
//!
//! Layout: `buffer[pixel * 3 + channel][bit_plane]`
//! - 3 channels per pixel (configurable order: GRB, RGB, etc.)
//! - 8 bit-planes per channel (MSB first)
//!
//! Double-buffered: front buffer is read by DMA, back buffer is written by CPU.

use super::color::Rgb;
use crate::state::ColorOrder;

/// Maximum pixels per string
pub const MAX_PIXELS: usize = 512;

/// Maximum number of parallel strings
pub const MAX_STRINGS: usize = 32;

/// Number of bit-planes per color channel (8 bits)
const VALUE_PLANES: usize = 8;

/// Number of channels (GRB = 3)
const CHANNELS: usize = 3;

/// Bit-plane encoded data for one color channel at one pixel position.
/// planes[0] = MSB, planes[7] = LSB
/// Each u32: bit N = value for string N
#[derive(Clone, Copy)]
#[repr(C)]
pub struct ValueBits {
    pub planes: [u32; VALUE_PLANES],
}

impl Default for ValueBits {
    fn default() -> Self {
        Self { planes: [0; VALUE_PLANES] }
    }
}

/// Single buffer for one frame
type FrameBuffer = [[ValueBits; CHANNELS]; MAX_PIXELS];

/// Per-string configuration for the LED driver
#[derive(Clone, Copy)]
pub struct StringConfig {
    /// Number of pixels in this string
    pub length: u16,
    /// Color order (RGB, GRB, etc.)
    pub color_order: ColorOrder,
}

impl Default for StringConfig {
    fn default() -> Self {
        Self {
            length: 50,
            color_order: ColorOrder::RGB,
        }
    }
}

/// Double-buffered LED buffer with bit-plane encoding for parallel output.
///
/// Stores pixel data for up to 32 LED strings simultaneously.
/// Uses double buffering: CPU writes to back buffer while DMA reads front buffer.
pub struct LedBuffer {
    /// Double buffer: [0] and [1]
    buffers: [FrameBuffer; 2],
    /// Which buffer is the back buffer (CPU writes here)
    back_buffer: u8,
    /// Per-string configuration (length, color order)
    strings: [StringConfig; MAX_STRINGS],
    /// Max length across all strings (for DMA output sizing)
    max_length: u16,
}

impl LedBuffer {
    /// Create a new double-buffered LED buffer with default config (50px, RGB)
    pub const fn new() -> Self {
        Self {
            buffers: [[[ValueBits { planes: [0; VALUE_PLANES] }; CHANNELS]; MAX_PIXELS]; 2],
            back_buffer: 0,
            strings: [StringConfig { length: 50, color_order: ColorOrder::RGB }; MAX_STRINGS],
            max_length: 50,
        }
    }

    /// Configure a single string
    pub fn set_string_config(&mut self, string: u8, config: StringConfig) {
        if (string as usize) < MAX_STRINGS {
            self.strings[string as usize] = config;
            self.update_max_length();
        }
    }

    /// Configure all strings at once
    pub fn set_all_strings(&mut self, configs: &[StringConfig; MAX_STRINGS]) {
        self.strings = *configs;
        self.update_max_length();
    }

    /// Set all strings to same length and color order (convenience for tests)
    pub fn set_uniform_config(&mut self, length: u16, color_order: ColorOrder) {
        for s in &mut self.strings {
            s.length = length;
            s.color_order = color_order;
        }
        self.max_length = length;
    }

    /// Get config for a string
    pub fn string_config(&self, string: u8) -> StringConfig {
        if (string as usize) < MAX_STRINGS {
            self.strings[string as usize]
        } else {
            StringConfig::default()
        }
    }

    /// Get max length across all strings (for DMA sizing)
    pub fn max_length(&self) -> u16 {
        self.max_length
    }

    fn update_max_length(&mut self) {
        self.max_length = self.strings.iter().map(|s| s.length).max().unwrap_or(0);
    }

    /// Swap buffers. Call after DMA transfer is complete.
    /// Returns the new front buffer index for DMA.
    pub fn swap(&mut self) -> u8 {
        self.back_buffer ^= 1;
        // Front buffer is the opposite of back
        self.back_buffer ^ 1
    }

    /// Get the front buffer index (for DMA to read)
    pub fn front_buffer_index(&self) -> u8 {
        self.back_buffer ^ 1
    }

    /// Set a pixel color for a specific string (writes to back buffer).
    ///
    /// Uses per-string color order from config.
    pub fn set_pixel(&mut self, string: u8, pixel: u16, color: Rgb) {
        if string >= MAX_STRINGS as u8 || pixel >= MAX_PIXELS as u16 {
            return;
        }

        let pixel_idx = pixel as usize;
        let mask = 1u32 << string;
        let buffer = &mut self.buffers[self.back_buffer as usize];

        // Reorder channels based on this string's color order
        let order = self.strings[string as usize].color_order;
        let channels = match order {
            ColorOrder::GRB => [color.g, color.r, color.b],
            ColorOrder::RGB => [color.r, color.g, color.b],
            ColorOrder::BGR => [color.b, color.g, color.r],
            ColorOrder::RBG => [color.r, color.b, color.g],
            ColorOrder::GBR => [color.g, color.b, color.r],
            ColorOrder::BRG => [color.b, color.r, color.g],
        };

        for (ch, &value) in channels.iter().enumerate() {
            let dest = &mut buffer[pixel_idx][ch];

            for bit in 0..8 {
                let color_bit = (value >> (7 - bit)) & 1;
                if color_bit != 0 {
                    dest.planes[bit] |= mask;
                } else {
                    dest.planes[bit] &= !mask;
                }
            }
        }
    }

    /// Get a pixel color for a specific string from back buffer (for testing).
    pub fn get_pixel(&self, string: u8, pixel: u16) -> Rgb {
        if string >= MAX_STRINGS as u8 || pixel >= MAX_PIXELS as u16 {
            return Rgb::BLACK;
        }

        let pixel_idx = pixel as usize;
        let mask = 1u32 << string;
        let buffer = &self.buffers[self.back_buffer as usize];

        let mut channels = [0u8; 3];

        for (ch, value) in channels.iter_mut().enumerate() {
            let src = &buffer[pixel_idx][ch];

            for bit in 0..8 {
                if src.planes[bit] & mask != 0 {
                    *value |= 1 << (7 - bit);
                }
            }
        }

        // Decode RGB back (was GRB: channels[1], channels[0], channels[2])
        Rgb::new(channels[0], channels[1], channels[2])
    }

    /// Clear back buffer to black.
    pub fn clear(&mut self) {
        let buffer = &mut self.buffers[self.back_buffer as usize];
        for pixel in 0..self.max_length as usize {
            for ch in 0..CHANNELS {
                buffer[pixel][ch].planes = [0; VALUE_PLANES];
            }
        }
    }

    /// Get front buffer as contiguous u32 slice for DMA.
    ///
    /// The buffer is laid out as:
    /// `[pixel0_ch0_planes, pixel0_ch1_planes, pixel0_ch2_planes, pixel1_ch0_planes, ...]`
    pub fn as_dma_words(&self) -> &[u32] {
        let front = (self.back_buffer ^ 1) as usize;
        let count = self.max_length as usize * CHANNELS * VALUE_PLANES;
        // Safety: ValueBits is repr(C) and contains only u32s
        unsafe {
            core::slice::from_raw_parts(
                self.buffers[front].as_ptr() as *const u32,
                count,
            )
        }
    }
}

impl Default for LedBuffer {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_set_get_pixel() {
        let mut buffer = LedBuffer::new();

        // Set pixel 0 on string 0 to red
        buffer.set_pixel(0, 0, Rgb::RED);
        assert_eq!(buffer.get_pixel(0, 0), Rgb::RED);

        // Set pixel 10 on string 5 to green
        buffer.set_pixel(5, 10, Rgb::GREEN);
        assert_eq!(buffer.get_pixel(5, 10), Rgb::GREEN);

        // Other pixels should be black
        assert_eq!(buffer.get_pixel(0, 1), Rgb::BLACK);
        assert_eq!(buffer.get_pixel(1, 0), Rgb::BLACK);
    }

    #[test]
    fn test_multiple_strings_same_pixel() {
        let mut buffer = LedBuffer::new();

        // Set same pixel on different strings
        buffer.set_pixel(0, 0, Rgb::RED);
        buffer.set_pixel(1, 0, Rgb::GREEN);
        buffer.set_pixel(2, 0, Rgb::BLUE);

        assert_eq!(buffer.get_pixel(0, 0), Rgb::RED);
        assert_eq!(buffer.get_pixel(1, 0), Rgb::GREEN);
        assert_eq!(buffer.get_pixel(2, 0), Rgb::BLUE);
    }

    #[test]
    fn test_bit_plane_encoding_rgb() {
        let mut buffer = LedBuffer::new();
        // Default is RGB order

        // Set pixel with known value: R=0b10101010, G=0b11001100, B=0b11110000
        buffer.set_pixel(0, 0, Rgb::new(0b10101010, 0b11001100, 0b11110000));

        // Check RGB encoding in bit-planes
        let pixel = &buffer.buffers[buffer.back_buffer as usize][0];

        // Channel 0 (R = 0b10101010)
        assert_eq!(pixel[0].planes[0] & 1, 1); // bit 7
        assert_eq!(pixel[0].planes[1] & 1, 0); // bit 6
        assert_eq!(pixel[0].planes[2] & 1, 1); // bit 5
        assert_eq!(pixel[0].planes[3] & 1, 0); // bit 4
        assert_eq!(pixel[0].planes[4] & 1, 1); // bit 3
        assert_eq!(pixel[0].planes[5] & 1, 0); // bit 2
        assert_eq!(pixel[0].planes[6] & 1, 1); // bit 1
        assert_eq!(pixel[0].planes[7] & 1, 0); // bit 0
    }

    #[test]
    fn test_bit_plane_encoding_grb() {
        let mut buffer = LedBuffer::new();
        // Set string 0 to GRB (WS2812 order)
        buffer.set_string_config(0, StringConfig { length: 50, color_order: ColorOrder::GRB });

        // Set pixel with known value: R=0b10101010, G=0b11001100, B=0b11110000
        buffer.set_pixel(0, 0, Rgb::new(0b10101010, 0b11001100, 0b11110000));

        // Check GRB encoding in bit-planes
        // Wire order: G, R, B
        let pixel = &buffer.buffers[buffer.back_buffer as usize][0];

        // Channel 0 (G = 0b11001100)
        assert_eq!(pixel[0].planes[0] & 1, 1); // bit 7
        assert_eq!(pixel[0].planes[1] & 1, 1); // bit 6
        assert_eq!(pixel[0].planes[2] & 1, 0); // bit 5
        assert_eq!(pixel[0].planes[3] & 1, 0); // bit 4
        assert_eq!(pixel[0].planes[4] & 1, 1); // bit 3
        assert_eq!(pixel[0].planes[5] & 1, 1); // bit 2
        assert_eq!(pixel[0].planes[6] & 1, 0); // bit 1
        assert_eq!(pixel[0].planes[7] & 1, 0); // bit 0
    }

    #[test]
    fn test_clear() {
        let mut buffer = LedBuffer::new();
        buffer.set_uniform_config(10, ColorOrder::RGB);

        // Set some pixels
        buffer.set_pixel(0, 0, Rgb::RED);
        buffer.set_pixel(1, 5, Rgb::GREEN);

        // Clear
        buffer.clear();

        // All should be black
        assert_eq!(buffer.get_pixel(0, 0), Rgb::BLACK);
        assert_eq!(buffer.get_pixel(1, 5), Rgb::BLACK);
    }

    #[test]
    fn test_bounds_check() {
        let mut buffer = LedBuffer::new();

        // Out of bounds should not panic
        buffer.set_pixel(32, 0, Rgb::RED); // string 32 is out of range
        buffer.set_pixel(0, 512, Rgb::RED); // pixel 512 is out of range

        // Should return black for out of bounds reads
        assert_eq!(buffer.get_pixel(32, 0), Rgb::BLACK);
        assert_eq!(buffer.get_pixel(0, 512), Rgb::BLACK);
    }
}
