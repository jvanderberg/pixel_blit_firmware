//! Frame generators for LED output.

use core::sync::atomic::{AtomicU8, AtomicU32, Ordering};
use pixel_blit::led_driver::{self, Frame, FrameGenerator};
use pixel_blit::state::ColorOrder;

/// Rainbow test generator - displays rainbow on selected string, red on others.
pub struct RainbowGenerator {
    current_string: AtomicU8,
    hue_offset: AtomicU8,
}

impl RainbowGenerator {
    pub const fn new() -> Self {
        Self {
            current_string: AtomicU8::new(0),
            hue_offset: AtomicU8::new(0),
        }
    }

    pub fn set_string(&self, string: u8) {
        self.current_string.store(string, Ordering::Relaxed);
    }
}

impl FrameGenerator for RainbowGenerator {
    fn generate(&self, frame: &mut Frame) {
        const PIXELS: u16 = 50;
        frame.set_uniform_config(PIXELS, ColorOrder::RGB);

        let selected = self.current_string.load(Ordering::Relaxed);
        let hue = self.hue_offset.fetch_add(1, Ordering::Relaxed);

        for string in 0..32u8 {
            for pixel in 0..PIXELS {
                let color = if string == selected {
                    let h = hue.wrapping_add((pixel as u8).wrapping_mul(5));
                    led_driver::hsv_to_rgb(h, 255, 255)
                } else {
                    led_driver::Rgb::RED
                };
                frame.set_pixel(string, pixel, color);
            }
        }
    }

    fn target_fps(&self) -> u16 {
        60
    }
}

/// String length generator - lights a single pixel on selected string.
pub struct StringLengthGenerator {
    /// Packed position: bits 16-23 = string (u8), bits 0-15 = pixel (u16)
    position: AtomicU32,
}

impl StringLengthGenerator {
    pub const fn new() -> Self {
        Self {
            position: AtomicU32::new(1),
        }
    }

    pub fn set_position(&self, string: u8, pixel: u16) {
        let packed = ((string as u32) << 16) | (pixel as u32);
        self.position.store(packed, Ordering::Relaxed);
    }
}

impl FrameGenerator for StringLengthGenerator {
    fn generate(&self, frame: &mut Frame) {
        let packed = self.position.load(Ordering::Relaxed);
        let string = (packed >> 16) as u8;
        let pixel = (packed & 0xFFFF) as u16;

        frame.set_uniform_config(pixel + 1, ColorOrder::RGB);
        frame.clear();
        frame.set_pixel(string, pixel, led_driver::Rgb::RED);
    }

    fn target_fps(&self) -> u16 {
        30
    }
}

// Static instances
pub static RAINBOW: RainbowGenerator = RainbowGenerator::new();
pub static STRING_LENGTH: StringLengthGenerator = StringLengthGenerator::new();
