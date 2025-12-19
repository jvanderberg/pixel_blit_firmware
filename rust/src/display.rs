//! Display abstraction for views.
//!
//! Views render to a `Display` which wraps embedded-graphics' `DrawTarget`.
//! This allows views to be tested without sh1106 hardware dependencies.

use embedded_graphics::{pixelcolor::BinaryColor, prelude::*};

#[cfg(feature = "firmware")]
use embedded_graphics::{
    mono_font::{ascii::FONT_6X10, MonoTextStyleBuilder},
    primitives::{PrimitiveStyle, Rectangle},
    text::Text,
};

#[cfg(feature = "firmware")]
use sh1106::{interface::DisplayInterface, prelude::*};

/// Display dimensions
pub const DISPLAY_WIDTH: u32 = 128;
pub const DISPLAY_HEIGHT: u32 = 64;

/// Display trait that views render to.
/// Views use these methods instead of raw embedded-graphics for testability.
pub trait Display: DrawTarget<Color = BinaryColor> + Sized {
    /// Clear the display buffer
    fn clear_buffer(&mut self);

    /// Flush the buffer to the physical display (no-op for mock)
    fn flush(&mut self) -> Result<(), Self::Error>;

    /// Draw text at position. Mock captures text, hardware renders pixels.
    fn text(&mut self, s: &str, x: i32, y: i32);

    /// Draw a filled rectangle
    fn fill_rect(&mut self, x: i32, y: i32, width: u32, height: u32);

    /// Get captured text content (for testing)
    fn get_text(&self) -> &str {
        ""
    }
}

/// Mock display for testing - captures text content for assertions
#[cfg(test)]
pub struct MockDisplay {
    pixels: [[bool; DISPLAY_WIDTH as usize]; DISPLAY_HEIGHT as usize],
    text_buffer: heapless::String<512>,
}

#[cfg(test)]
impl MockDisplay {
    pub fn new() -> Self {
        Self {
            pixels: [[false; DISPLAY_WIDTH as usize]; DISPLAY_HEIGHT as usize],
            text_buffer: heapless::String::new(),
        }
    }

    /// Check if captured text contains a substring
    pub fn contains(&self, s: &str) -> bool {
        self.text_buffer.contains(s)
    }

    /// Check if a pixel is set
    pub fn get_pixel(&self, x: u32, y: u32) -> bool {
        if x < DISPLAY_WIDTH && y < DISPLAY_HEIGHT {
            self.pixels[y as usize][x as usize]
        } else {
            false
        }
    }

    /// Count set pixels in a region
    pub fn count_pixels_in_region(&self, x: u32, y: u32, width: u32, height: u32) -> u32 {
        let mut count = 0;
        for py in y..(y + height).min(DISPLAY_HEIGHT) {
            for px in x..(x + width).min(DISPLAY_WIDTH) {
                if self.pixels[py as usize][px as usize] {
                    count += 1;
                }
            }
        }
        count
    }

    /// Check if any pixels are set
    pub fn has_any_pixels(&self) -> bool {
        for row in &self.pixels {
            for &pixel in row {
                if pixel {
                    return true;
                }
            }
        }
        false
    }
}

#[cfg(test)]
impl DrawTarget for MockDisplay {
    type Color = BinaryColor;
    type Error = core::convert::Infallible;

    fn draw_iter<I>(&mut self, pixels: I) -> Result<(), Self::Error>
    where
        I: IntoIterator<Item = Pixel<Self::Color>>,
    {
        for Pixel(coord, color) in pixels {
            if coord.x >= 0
                && coord.x < DISPLAY_WIDTH as i32
                && coord.y >= 0
                && coord.y < DISPLAY_HEIGHT as i32
            {
                self.pixels[coord.y as usize][coord.x as usize] = color == BinaryColor::On;
            }
        }
        Ok(())
    }
}

#[cfg(test)]
impl OriginDimensions for MockDisplay {
    fn size(&self) -> Size {
        Size::new(DISPLAY_WIDTH, DISPLAY_HEIGHT)
    }
}

#[cfg(test)]
impl Display for MockDisplay {
    fn clear_buffer(&mut self) {
        self.pixels = [[false; DISPLAY_WIDTH as usize]; DISPLAY_HEIGHT as usize];
        self.text_buffer.clear();
    }

    fn flush(&mut self) -> Result<(), Self::Error> {
        Ok(()) // No-op for mock
    }

    fn text(&mut self, s: &str, _x: i32, _y: i32) {
        // Capture text for test assertions (ignore position)
        self.text_buffer.push_str(s).ok();
        self.text_buffer.push_str(" ").ok(); // Space between texts
    }

    fn fill_rect(&mut self, x: i32, y: i32, width: u32, height: u32) {
        // Draw filled rectangle to pixel buffer
        for py in y..(y + height as i32) {
            for px in x..(x + width as i32) {
                if px >= 0 && px < DISPLAY_WIDTH as i32 && py >= 0 && py < DISPLAY_HEIGHT as i32 {
                    self.pixels[py as usize][px as usize] = true;
                }
            }
        }
    }

    fn get_text(&self) -> &str {
        &self.text_buffer
    }
}

// ============================================================================
// SH1106 Hardware Display (firmware only)
// ============================================================================

/// Wrapper for sh1106 GraphicsMode that implements our Display trait.
#[cfg(feature = "firmware")]
pub struct Sh1106Display<DI: DisplayInterface> {
    inner: GraphicsMode<DI>,
}

#[cfg(feature = "firmware")]
impl<DI: DisplayInterface> Sh1106Display<DI> {
    pub fn new(inner: GraphicsMode<DI>) -> Self {
        Self { inner }
    }

    /// Initialize the display hardware
    pub fn init(&mut self) -> Result<(), DI::Error> {
        self.inner.init()
    }
}

#[cfg(feature = "firmware")]
impl<DI: DisplayInterface> DrawTarget for Sh1106Display<DI> {
    type Color = BinaryColor;
    type Error = DI::Error;

    fn draw_iter<I>(&mut self, pixels: I) -> Result<(), Self::Error>
    where
        I: IntoIterator<Item = Pixel<Self::Color>>,
    {
        // GraphicsMode's draw_iter never fails (returns Infallible)
        self.inner.draw_iter(pixels).ok();
        Ok(())
    }
}

#[cfg(feature = "firmware")]
impl<DI: DisplayInterface> OriginDimensions for Sh1106Display<DI> {
    fn size(&self) -> Size {
        Size::new(DISPLAY_WIDTH, DISPLAY_HEIGHT)
    }
}

#[cfg(feature = "firmware")]
impl<DI: DisplayInterface> Display for Sh1106Display<DI> {
    fn clear_buffer(&mut self) {
        self.inner.clear();
    }

    fn flush(&mut self) -> Result<(), Self::Error> {
        self.inner.flush()
    }

    fn text(&mut self, s: &str, x: i32, y: i32) {
        let style = MonoTextStyleBuilder::new()
            .font(&FONT_6X10)
            .text_color(BinaryColor::On)
            .build();
        Text::new(s, Point::new(x, y), style)
            .draw(&mut self.inner)
            .ok();
    }

    fn fill_rect(&mut self, x: i32, y: i32, width: u32, height: u32) {
        Rectangle::new(Point::new(x, y), Size::new(width, height))
            .into_styled(PrimitiveStyle::with_fill(BinaryColor::On))
            .draw(&mut self.inner)
            .ok();
    }
}
