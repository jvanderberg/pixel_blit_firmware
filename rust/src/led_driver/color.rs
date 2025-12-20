//! Color utilities for LED driver.
//!
//! Ported from pb_led_driver_color.c

/// RGB color packed as 0x00RRGGBB
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Rgb {
    pub r: u8,
    pub g: u8,
    pub b: u8,
}

impl Rgb {
    pub const fn new(r: u8, g: u8, b: u8) -> Self {
        Self { r, g, b }
    }

    /// Red color
    pub const RED: Self = Self::new(255, 0, 0);
    /// Green color
    pub const GREEN: Self = Self::new(0, 255, 0);
    /// Blue color
    pub const BLUE: Self = Self::new(0, 0, 255);
    /// Black (off)
    pub const BLACK: Self = Self::new(0, 0, 0);
    /// White
    pub const WHITE: Self = Self::new(255, 255, 255);

    /// Pack into 0x00RRGGBB format
    pub const fn to_packed(self) -> u32 {
        ((self.r as u32) << 16) | ((self.g as u32) << 8) | (self.b as u32)
    }

    /// Unpack from 0x00RRGGBB format
    pub const fn from_packed(packed: u32) -> Self {
        Self {
            r: ((packed >> 16) & 0xFF) as u8,
            g: ((packed >> 8) & 0xFF) as u8,
            b: (packed & 0xFF) as u8,
        }
    }

    /// Scale brightness (0-255, where 255 = full brightness)
    pub fn scale(self, scale: u8) -> Self {
        Self {
            r: ((self.r as u16 * scale as u16) / 255) as u8,
            g: ((self.g as u16 * scale as u16) / 255) as u8,
            b: ((self.b as u16 * scale as u16) / 255) as u8,
        }
    }
}

/// Convert HSV to RGB.
///
/// Fast integer HSV to RGB conversion.
/// - h: 0-255 (hue, wraps around)
/// - s: 0-255 (saturation)
/// - v: 0-255 (value/brightness)
pub fn hsv_to_rgb(h: u8, s: u8, v: u8) -> Rgb {
    if s == 0 {
        // Grayscale
        return Rgb::new(v, v, v);
    }

    // Scale h to 0-1535 (6 * 256) for 6 sectors
    let h6 = (h as u16) * 6;
    let sector = (h6 >> 8) as u8; // 0-5
    let frac = (h6 & 0xFF) as u8; // Fractional part within sector

    // Calculate intermediate values
    let p = ((v as u16 * (255 - s) as u16) / 255) as u8;
    let q = ((v as u16 * (255 - ((s as u16 * frac as u16) / 255) as u8) as u16) / 255) as u8;
    let t = ((v as u16 * (255 - ((s as u16 * (255 - frac) as u16) / 255) as u8) as u16) / 255) as u8;

    match sector {
        0 => Rgb::new(v, t, p), // Red to Yellow
        1 => Rgb::new(q, v, p), // Yellow to Green
        2 => Rgb::new(p, v, t), // Green to Cyan
        3 => Rgb::new(p, q, v), // Cyan to Blue
        4 => Rgb::new(t, p, v), // Blue to Magenta
        _ => Rgb::new(v, p, q), // Magenta to Red
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_hsv_red() {
        let rgb = hsv_to_rgb(0, 255, 255);
        assert_eq!(rgb.r, 255);
        assert_eq!(rgb.g, 0);
        assert_eq!(rgb.b, 0);
    }

    #[test]
    fn test_hsv_green() {
        // Green is at hue ~85 (255/3)
        let rgb = hsv_to_rgb(85, 255, 255);
        assert_eq!(rgb.g, 255);
        assert!(rgb.r < 10);
        assert!(rgb.b < 10);
    }

    #[test]
    fn test_hsv_blue() {
        // Blue is at hue ~170 (255*2/3)
        let rgb = hsv_to_rgb(170, 255, 255);
        assert_eq!(rgb.b, 255);
        assert!(rgb.r < 10);
        assert!(rgb.g < 10);
    }

    #[test]
    fn test_hsv_grayscale() {
        let rgb = hsv_to_rgb(0, 0, 128);
        assert_eq!(rgb.r, 128);
        assert_eq!(rgb.g, 128);
        assert_eq!(rgb.b, 128);
    }

    #[test]
    fn test_scale() {
        let white = Rgb::WHITE;
        let half = white.scale(128);
        // 255 * 128 / 255 = 128
        assert_eq!(half.r, 128);
        assert_eq!(half.g, 128);
        assert_eq!(half.b, 128);
    }

    #[test]
    fn test_packed() {
        let rgb = Rgb::new(0x12, 0x34, 0x56);
        assert_eq!(rgb.to_packed(), 0x00123456);
        assert_eq!(Rgb::from_packed(0x00123456), rgb);
    }
}
