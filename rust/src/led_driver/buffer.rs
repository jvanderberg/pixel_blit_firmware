//! Bit-plane buffer for parallel WS2812 output.
//!
//! Uses a token-based ownership system for safe double-buffering.
//!
//! # Architecture
//!
//! Two static frame buffers exist. Ownership is tracked by tokens:
//! - `WriteToken` - grants exclusive write access to one buffer
//! - `ReadToken` - grants read access (for DMA) to one buffer
//!
//! Tokens are passed through channels between tasks:
//! ```text
//! Runner (writer)              Output (reader)
//!       |                            |
//!       v                            v
//!  [WriteToken(0)]              [waiting]
//!       |                            |
//!  write pixels                      |
//!       |                            |
//!  send ReadToken(0) ------>   receive ReadToken(0)
//!       |                            |
//!  receive WriteToken(1) <--   [do DMA on buffer 0]
//!       |                            |
//!  write pixels                 send WriteToken(0)
//!       |                            |
//! ```
//!
//! # Safety
//!
//! The token system guarantees exclusive access:
//! - Only one WriteToken OR ReadToken exists per buffer index at a time
//! - Tokens are passed through channels, never duplicated
//! - The type system prevents accessing a buffer without holding its token
//!
//! Small `unsafe` blocks in token methods are sound because the channel-based
//! protocol enforces exclusive access at runtime.

use super::color::Rgb;
use crate::state::ColorOrder;

/// Maximum pixels per string
pub const MAX_PIXELS: usize = 512;

/// Maximum number of parallel strings
pub const MAX_STRINGS: usize = 32;

/// Number of bit-planes per color channel (8 bits)
const VALUE_PLANES: usize = 8;

/// Number of channels (RGB = 3)
const CHANNELS: usize = 3;

/// Bit-plane encoded data for one color channel at one pixel position.
/// planes[0] = MSB, planes[7] = LSB
/// Each u32: bit N = value for string N
#[derive(Clone, Copy)]
#[repr(C)]
pub struct ValueBits {
    pub planes: [u32; VALUE_PLANES],
}

impl ValueBits {
    pub const fn new() -> Self {
        Self { planes: [0; VALUE_PLANES] }
    }
}

impl Default for ValueBits {
    fn default() -> Self {
        Self::new()
    }
}

/// Per-string configuration for frame generation.
///
/// This is separate from `state::StringConfig` because:
/// - This is embedded in `FrameData` and needs `#[repr(C)]` for DMA
/// - Uses `length` (how many pixels to output) vs `pixel_count` (configured count)
/// - May differ per-frame (e.g., during string length calibration)
#[derive(Clone, Copy)]
#[repr(C)]
pub struct FrameStringConfig {
    /// Number of pixels to output for this string
    pub length: u16,
    /// Color order (RGB, GRB, etc.)
    pub color_order: ColorOrder,
}

impl Default for FrameStringConfig {
    fn default() -> Self {
        Self {
            length: 50,
            color_order: ColorOrder::RGB,
        }
    }
}

/// Frame data storage (the actual pixel data).
///
/// # Memory Layout
///
/// This struct is `#[repr(C)]` to guarantee predictable memory layout for DMA.
/// The `data` array is laid out as contiguous `u32` values:
/// - For each pixel (0..MAX_PIXELS):
///   - For each channel (0..3 = R, G, B after color order transform):
///     - 8 bit-planes (MSB first), each a u32 where bit N = string N's value
///
/// Total size: MAX_PIXELS * 3 * 8 * 4 = 512 * 3 * 8 * 4 = 49,152 bytes per buffer
#[repr(C)]
pub struct FrameData {
    /// Bit-plane encoded pixel data
    data: [[ValueBits; CHANNELS]; MAX_PIXELS],
    /// Per-string configuration
    strings: [FrameStringConfig; MAX_STRINGS],
    /// Max length across all strings (for DMA sizing)
    max_length: u16,
}

impl FrameData {
    pub const fn new() -> Self {
        Self {
            data: [[ValueBits::new(); CHANNELS]; MAX_PIXELS],
            strings: [FrameStringConfig { length: 50, color_order: ColorOrder::RGB }; MAX_STRINGS],
            max_length: 50,
        }
    }

    /// Set a pixel color for a specific string
    pub fn set_pixel(&mut self, string: u8, pixel: u16, color: Rgb) {
        if string >= MAX_STRINGS as u8 || pixel >= MAX_PIXELS as u16 {
            return;
        }

        let pixel_idx = pixel as usize;
        let mask = 1u32 << string;

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
            let dest = &mut self.data[pixel_idx][ch];

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

    /// Set all pixels for a string from raw RGB data (optimized batch operation).
    ///
    /// `rgb_data` should be packed RGB bytes: [R0, G0, B0, R1, G1, B1, ...]
    /// This is much faster than calling set_pixel() for each pixel.
    pub fn set_string_pixels(&mut self, string: u8, rgb_data: &[u8]) {
        if string >= MAX_STRINGS as u8 {
            return;
        }

        let mask = 1u32 << string;
        let not_mask = !mask;
        let order = self.strings[string as usize].color_order;

        let pixel_count = rgb_data.len() / 3;

        for pixel_idx in 0..pixel_count {
            let base = pixel_idx * 3;

            // Get raw RGB from input
            let r = rgb_data[base] as u32;
            let g = rgb_data[base + 1] as u32;
            let b = rgb_data[base + 2] as u32;

            // Reorder based on color order and get channel values
            let (c0, c1, c2) = match order {
                ColorOrder::RGB => (r, g, b),
                ColorOrder::GRB => (g, r, b),
                ColorOrder::BGR => (b, g, r),
                ColorOrder::RBG => (r, b, g),
                ColorOrder::GBR => (g, b, r),
                ColorOrder::BRG => (b, r, g),
            };

            // Channel 0
            let dest = &mut self.data[pixel_idx][0];
            dest.planes[0] = (dest.planes[0] & not_mask) | (((c0 >> 7) & 1) << string);
            dest.planes[1] = (dest.planes[1] & not_mask) | (((c0 >> 6) & 1) << string);
            dest.planes[2] = (dest.planes[2] & not_mask) | (((c0 >> 5) & 1) << string);
            dest.planes[3] = (dest.planes[3] & not_mask) | (((c0 >> 4) & 1) << string);
            dest.planes[4] = (dest.planes[4] & not_mask) | (((c0 >> 3) & 1) << string);
            dest.planes[5] = (dest.planes[5] & not_mask) | (((c0 >> 2) & 1) << string);
            dest.planes[6] = (dest.planes[6] & not_mask) | (((c0 >> 1) & 1) << string);
            dest.planes[7] = (dest.planes[7] & not_mask) | ((c0 & 1) << string);

            // Channel 1
            let dest = &mut self.data[pixel_idx][1];
            dest.planes[0] = (dest.planes[0] & not_mask) | (((c1 >> 7) & 1) << string);
            dest.planes[1] = (dest.planes[1] & not_mask) | (((c1 >> 6) & 1) << string);
            dest.planes[2] = (dest.planes[2] & not_mask) | (((c1 >> 5) & 1) << string);
            dest.planes[3] = (dest.planes[3] & not_mask) | (((c1 >> 4) & 1) << string);
            dest.planes[4] = (dest.planes[4] & not_mask) | (((c1 >> 3) & 1) << string);
            dest.planes[5] = (dest.planes[5] & not_mask) | (((c1 >> 2) & 1) << string);
            dest.planes[6] = (dest.planes[6] & not_mask) | (((c1 >> 1) & 1) << string);
            dest.planes[7] = (dest.planes[7] & not_mask) | ((c1 & 1) << string);

            // Channel 2
            let dest = &mut self.data[pixel_idx][2];
            dest.planes[0] = (dest.planes[0] & not_mask) | (((c2 >> 7) & 1) << string);
            dest.planes[1] = (dest.planes[1] & not_mask) | (((c2 >> 6) & 1) << string);
            dest.planes[2] = (dest.planes[2] & not_mask) | (((c2 >> 5) & 1) << string);
            dest.planes[3] = (dest.planes[3] & not_mask) | (((c2 >> 4) & 1) << string);
            dest.planes[4] = (dest.planes[4] & not_mask) | (((c2 >> 3) & 1) << string);
            dest.planes[5] = (dest.planes[5] & not_mask) | (((c2 >> 2) & 1) << string);
            dest.planes[6] = (dest.planes[6] & not_mask) | (((c2 >> 1) & 1) << string);
            dest.planes[7] = (dest.planes[7] & not_mask) | ((c2 & 1) << string);
        }
    }

    /// Set all pixels for all strings from FSEQ frame data using transpose.
    ///
    /// This processes all 32 strings simultaneously for each pixel position,
    /// transposing the bit matrix to write planes directly without read-modify-write.
    ///
    /// `frame_data`: Raw FSEQ frame (string 0 pixels, then string 1 pixels, etc.)
    /// `string_lengths`: Pixel count for each string
    /// `num_strings`: Number of active strings
    pub fn set_frame_transpose(
        &mut self,
        frame_data: &[u8],
        string_lengths: &[u16; 32],
        num_strings: u8,
    ) {
        // Precompute byte offsets where each string's data starts
        let mut string_offsets = [0usize; 32];
        let mut offset = 0usize;
        for i in 0..32 {
            string_offsets[i] = offset;
            offset += (string_lengths[i] as usize) * 3;
        }

        // Find max pixel count across all strings
        let max_pixels = string_lengths.iter().take(num_strings as usize).copied().max().unwrap_or(0) as usize;

        // Get color order for each string (cache it)
        let mut orders = [ColorOrder::RGB; 32];
        for i in 0..32 {
            orders[i] = self.strings[i].color_order;
        }

        // Process each pixel position across all strings
        for pixel_idx in 0..max_pixels {
            // Gather 32 bytes for each channel at this pixel position
            let mut r_bytes = [0u8; 32];
            let mut g_bytes = [0u8; 32];
            let mut b_bytes = [0u8; 32];

            for string_idx in 0..32 {
                if string_idx < num_strings as usize && pixel_idx < string_lengths[string_idx] as usize {
                    let byte_offset = string_offsets[string_idx] + pixel_idx * 3;
                    if byte_offset + 2 < frame_data.len() {
                        let r = frame_data[byte_offset];
                        let g = frame_data[byte_offset + 1];
                        let b = frame_data[byte_offset + 2];

                        // Apply color order transform
                        let (c0, c1, c2) = match orders[string_idx] {
                            ColorOrder::RGB => (r, g, b),
                            ColorOrder::GRB => (g, r, b),
                            ColorOrder::BGR => (b, g, r),
                            ColorOrder::RBG => (r, b, g),
                            ColorOrder::GBR => (g, b, r),
                            ColorOrder::BRG => (b, r, g),
                        };
                        r_bytes[string_idx] = c0;
                        g_bytes[string_idx] = c1;
                        b_bytes[string_idx] = c2;
                    }
                }
                // else: stays 0 (black) for strings that don't have this pixel
            }

            // Transpose each channel's 32 bytes into 8 u32 planes
            Self::transpose_32x8(&r_bytes, &mut self.data[pixel_idx][0].planes);
            Self::transpose_32x8(&g_bytes, &mut self.data[pixel_idx][1].planes);
            Self::transpose_32x8(&b_bytes, &mut self.data[pixel_idx][2].planes);
        }
    }

    /// Transpose a 32×8 bit matrix (32 bytes) into 8×32 bits (8 u32s).
    ///
    /// Input: 32 bytes, each byte has 8 bits
    /// Output: 8 u32s, where output[i] bit j = input[j] bit (7-i)
    #[inline]
    fn transpose_32x8(input: &[u8; 32], output: &mut [u32; 8]) {
        // Build each plane by collecting one bit from each of the 32 bytes
        for bit in 0..8 {
            let mut plane: u32 = 0;
            let shift = 7 - bit;
            for (string_idx, &byte) in input.iter().enumerate() {
                let bit_val = ((byte >> shift) & 1) as u32;
                plane |= bit_val << string_idx;
            }
            output[bit] = plane;
        }
    }

    /// Clear all pixels to black
    pub fn clear(&mut self) {
        for pixel in 0..self.max_length as usize {
            for ch in 0..CHANNELS {
                self.data[pixel][ch].planes = [0; VALUE_PLANES];
            }
        }
    }

    /// Configure a single string
    pub fn set_string_config(&mut self, string: u8, config: FrameStringConfig) {
        if (string as usize) < MAX_STRINGS {
            self.strings[string as usize] = config;
            self.update_max_length();
        }
    }

    /// Set all strings to same length and color order
    pub fn set_uniform_config(&mut self, length: u16, color_order: ColorOrder) {
        for s in &mut self.strings {
            s.length = length;
            s.color_order = color_order;
        }
        self.max_length = length;
    }

    /// Get config for a string
    pub fn string_config(&self, string: u8) -> FrameStringConfig {
        if (string as usize) < MAX_STRINGS {
            self.strings[string as usize]
        } else {
            FrameStringConfig::default()
        }
    }

    /// Get max length across all strings
    pub fn max_length(&self) -> u16 {
        self.max_length
    }

    fn update_max_length(&mut self) {
        self.max_length = self.strings.iter().map(|s| s.length).max().unwrap_or(0);
    }

    /// Get the number of u32 words needed for DMA
    pub fn dma_word_count(&self) -> usize {
        self.max_length as usize * CHANNELS * VALUE_PLANES
    }

    /// Get frame data as a slice of u32 for DMA.
    ///
    /// Layout: contiguous bit-planes for each pixel's channels.
    pub fn as_dma_slice(&self) -> &[u32] {
        let count = self.dma_word_count();
        // Safety: FrameData is repr(C) with contiguous u32 arrays.
        // The data layout matches what DMA expects.
        unsafe {
            core::slice::from_raw_parts(
                self.data.as_ptr() as *const u32,
                count,
            )
        }
    }
}

impl Default for FrameData {
    fn default() -> Self {
        Self::new()
    }
}

// ============================================================================
// Token-based Ownership System
// ============================================================================

/// Token proving exclusive write access to a buffer.
/// Only one WriteToken exists per buffer index at a time.
#[derive(Debug)]
pub struct WriteToken {
    index: u8,
}

impl WriteToken {
    /// Get the buffer index this token grants access to.
    pub fn index(&self) -> u8 {
        self.index
    }

    /// Convert to a ReadToken (transfers ownership to reader).
    pub fn into_read_token(self) -> ReadToken {
        ReadToken { index: self.index }
    }
}

/// Token proving exclusive read access to a buffer (for DMA).
/// Only one ReadToken exists per buffer index at a time.
#[derive(Debug)]
pub struct ReadToken {
    index: u8,
}

impl ReadToken {
    /// Get the buffer index this token grants access to.
    pub fn index(&self) -> u8 {
        self.index
    }

    /// Convert back to a WriteToken (transfers ownership to writer).
    pub fn into_write_token(self) -> WriteToken {
        WriteToken { index: self.index }
    }
}

// ============================================================================
// Firmware-only: Static Buffers and Channels
// ============================================================================

#[cfg(feature = "firmware")]
mod firmware {
    use super::*;
    use core::cell::UnsafeCell;
    use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
    use embassy_sync::channel::Channel;

    /// Wrapper to make UnsafeCell Sync.
    /// Safety: The token system ensures only one task accesses each buffer at a time.
    struct SyncCell(UnsafeCell<FrameData>);

    // Safety: Access is controlled by the token system. Only the holder of a
    // WriteToken or ReadToken for a given index can access that buffer.
    // Tokens are passed through channels and never duplicated.
    unsafe impl Sync for SyncCell {}

    impl SyncCell {
        const fn new() -> Self {
            Self(UnsafeCell::new(FrameData::new()))
        }

        fn get(&self) -> *mut FrameData {
            self.0.get()
        }
    }

    static BUFFER_0: SyncCell = SyncCell::new();
    static BUFFER_1: SyncCell = SyncCell::new();

    impl WriteToken {
        /// Get mutable access to the buffer.
        ///
        /// # Safety
        /// Safe because holding a WriteToken proves exclusive access.
        /// The token system (channels) ensures only one token exists per buffer.
        pub fn get_mut(&mut self) -> &mut FrameData {
            unsafe {
                match self.index {
                    0 => &mut *BUFFER_0.get(),
                    _ => &mut *BUFFER_1.get(),
                }
            }
        }
    }

    impl ReadToken {
        /// Get the DMA slice for this buffer.
        ///
        /// # Safety
        /// Safe because holding a ReadToken proves exclusive access.
        /// The slice is valid for the lifetime of the token (until converted back).
        pub fn as_dma_slice(&self) -> &'static [u32] {
            unsafe {
                match self.index {
                    0 => (*BUFFER_0.get()).as_dma_slice(),
                    _ => (*BUFFER_1.get()).as_dma_slice(),
                }
            }
        }

        /// Get the DMA word count for this buffer.
        pub fn dma_len(&self) -> usize {
            unsafe {
                match self.index {
                    0 => (*BUFFER_0.get()).dma_word_count(),
                    _ => (*BUFFER_1.get()).dma_word_count(),
                }
            }
        }
    }

    /// Channel capacity (only need 1 since we have 2 buffers)
    const CHANNEL_CAP: usize = 1;

    /// Channel for passing completed frames from writer to reader.
    pub static FRAME_CHANNEL: Channel<CriticalSectionRawMutex, ReadToken, CHANNEL_CAP> = Channel::new();

    /// Channel for returning buffers from reader to writer.
    pub static FREE_CHANNEL: Channel<CriticalSectionRawMutex, WriteToken, CHANNEL_CAP> = Channel::new();

    /// Channel for FSEQ task to request token takeover from runner.
    pub static FSEQ_REQUEST_CHANNEL: Channel<CriticalSectionRawMutex, (), CHANNEL_CAP> = Channel::new();

    /// Channel for runner to hand off token to FSEQ task.
    pub static FSEQ_TOKEN_CHANNEL: Channel<CriticalSectionRawMutex, WriteToken, CHANNEL_CAP> = Channel::new();

    /// Channel for FSEQ task to return token to runner.
    pub static FSEQ_RETURN_CHANNEL: Channel<CriticalSectionRawMutex, WriteToken, CHANNEL_CAP> = Channel::new();

    /// Initialize the buffer system. Call once at startup.
    /// Returns the initial WriteToken for the writer task.
    pub fn init() -> WriteToken {
        // Writer starts with buffer 0
        // Buffer 1's token goes into the free channel for later
        FREE_CHANNEL.try_send(WriteToken { index: 1 }).ok();

        WriteToken { index: 0 }
    }
}

#[cfg(feature = "firmware")]
pub use firmware::{init, FRAME_CHANNEL, FREE_CHANNEL, FSEQ_REQUEST_CHANNEL, FSEQ_TOKEN_CHANNEL, FSEQ_RETURN_CHANNEL};

// ============================================================================
// Tests
// ============================================================================

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_frame_data_set_pixel() {
        let mut frame = FrameData::new();
        frame.set_uniform_config(50, ColorOrder::RGB);

        frame.set_pixel(0, 0, Rgb::new(255, 0, 0));

        // Check that the bit planes are set correctly for red
        // R=255 means all 8 bits are 1 for string 0
        for bit in 0..8 {
            assert_eq!(frame.data[0][0].planes[bit] & 1, 1, "R bit {} should be set", bit);
            assert_eq!(frame.data[0][1].planes[bit] & 1, 0, "G bit {} should be 0", bit);
            assert_eq!(frame.data[0][2].planes[bit] & 1, 0, "B bit {} should be 0", bit);
        }
    }

    #[test]
    fn test_frame_data_grb_order() {
        let mut frame = FrameData::new();
        frame.set_string_config(0, FrameStringConfig { length: 50, color_order: ColorOrder::GRB });

        // Set red pixel - in GRB order, red goes to channel 1
        frame.set_pixel(0, 0, Rgb::new(255, 0, 0));

        // Channel 0 should be G (0), Channel 1 should be R (255)
        for bit in 0..8 {
            assert_eq!(frame.data[0][0].planes[bit] & 1, 0, "G bit {} should be 0", bit);
            assert_eq!(frame.data[0][1].planes[bit] & 1, 1, "R bit {} should be set", bit);
            assert_eq!(frame.data[0][2].planes[bit] & 1, 0, "B bit {} should be 0", bit);
        }
    }

    #[test]
    fn test_frame_data_clear() {
        let mut frame = FrameData::new();
        frame.set_uniform_config(10, ColorOrder::RGB);

        frame.set_pixel(0, 0, Rgb::new(255, 255, 255));
        frame.clear();

        // All planes should be 0
        for bit in 0..8 {
            assert_eq!(frame.data[0][0].planes[bit], 0);
        }
    }

    #[test]
    fn test_dma_slice_size() {
        let mut frame = FrameData::new();
        frame.set_uniform_config(2, ColorOrder::RGB); // 2 pixels

        // 2 pixels * 3 channels * 8 planes = 48 words
        assert_eq!(frame.dma_word_count(), 48);
    }

    #[test]
    fn test_token_conversion() {
        let write = WriteToken { index: 0 };
        assert_eq!(write.index(), 0);

        let read = write.into_read_token();
        assert_eq!(read.index(), 0);

        let write_back = read.into_write_token();
        assert_eq!(write_back.index(), 0);
    }

    #[test]
    fn test_token_conversion_both_buffers() {
        // Test buffer 0
        let write0 = WriteToken { index: 0 };
        let read0 = write0.into_read_token();
        let write0_back = read0.into_write_token();
        assert_eq!(write0_back.index(), 0);

        // Test buffer 1
        let write1 = WriteToken { index: 1 };
        let read1 = write1.into_read_token();
        let write1_back = read1.into_write_token();
        assert_eq!(write1_back.index(), 1);
    }

    #[test]
    fn test_token_protocol_simulation() {
        // Simulate the double-buffer protocol without async channels
        // This verifies the token ownership transfer pattern

        // Initial state: writer has buffer 0, buffer 1 is "in free channel"
        let mut writer_token = WriteToken { index: 0 };
        let mut free_token = Some(WriteToken { index: 1 });

        // Simulate 3 frames of the protocol
        for frame in 0..3 {
            // Writer fills buffer
            let buffer_idx = writer_token.index();

            // Writer sends to reader (convert Write -> Read)
            let reader_token = writer_token.into_read_token();
            assert_eq!(reader_token.index(), buffer_idx);

            // Writer gets next buffer from free channel
            writer_token = free_token.take().expect("free channel should have token");

            // Reader finishes DMA, returns buffer to free channel
            free_token = Some(reader_token.into_write_token());
            assert_eq!(free_token.as_ref().unwrap().index(), buffer_idx);

            // Verify buffers alternate: 0, 1, 0, 1, ...
            let expected = frame % 2;
            assert_eq!(buffer_idx, expected as u8, "frame {} should use buffer {}", frame, expected);
        }
    }

    #[test]
    fn test_bounds_check() {
        let mut frame = FrameData::new();

        // Out of bounds should not panic
        frame.set_pixel(32, 0, Rgb::new(255, 0, 0)); // string 32 is out of range
        frame.set_pixel(0, 512, Rgb::new(255, 0, 0)); // pixel 512 is out of range
    }

    #[test]
    fn test_frame_generation_pattern() {
        // Test that a typical generation pattern works correctly
        // This simulates what a generator would do
        let mut frame = FrameData::new();

        // Configure 32 strings with 10 pixels each
        frame.set_uniform_config(10, ColorOrder::GRB);
        assert_eq!(frame.max_length(), 10);

        // Generate a simple pattern: each string gets a different color
        for string in 0..32u8 {
            let hue = string.wrapping_mul(8); // Spread across hue range
            let color = Rgb::new(hue, 255 - hue, 128);
            for pixel in 0..10u16 {
                frame.set_pixel(string, pixel, color);
            }
        }

        // Verify DMA size is correct: 10 pixels * 3 channels * 8 planes = 240 words
        assert_eq!(frame.dma_word_count(), 240);

        // Verify we can read back the config
        let config = frame.string_config(0);
        assert_eq!(config.length, 10);
        assert_eq!(config.color_order, ColorOrder::GRB);
    }

    #[test]
    fn test_mixed_string_configs() {
        let mut frame = FrameData::new();

        // Configure different strings with different lengths/orders
        frame.set_string_config(0, FrameStringConfig { length: 50, color_order: ColorOrder::RGB });
        frame.set_string_config(1, FrameStringConfig { length: 100, color_order: ColorOrder::GRB });
        frame.set_string_config(2, FrameStringConfig { length: 25, color_order: ColorOrder::BGR });

        // Max should be the longest
        assert_eq!(frame.max_length(), 100);

        // Verify individual configs
        assert_eq!(frame.string_config(0).length, 50);
        assert_eq!(frame.string_config(1).length, 100);
        assert_eq!(frame.string_config(2).length, 25);
        assert_eq!(frame.string_config(0).color_order, ColorOrder::RGB);
        assert_eq!(frame.string_config(1).color_order, ColorOrder::GRB);
        assert_eq!(frame.string_config(2).color_order, ColorOrder::BGR);
    }
}
