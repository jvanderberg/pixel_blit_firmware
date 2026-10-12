//! Brightness view - adjust LED brightness level.

use core::fmt::Write;
use heapless::String;

use crate::display::Display;
use crate::state::{AppState, BRIGHTNESS_MAX};

pub struct BrightnessView;

impl BrightnessView {
    pub fn render<D: Display>(display: &mut D, state: &AppState) {
        // Title
        display.text("Brightness", 32, 10);

        // Level display
        let mut line: String<32> = String::new();
        write!(line, "Level: {} / {}", state.brightness_level, BRIGHTNESS_MAX).ok();
        display.text(&line, 28, 28);

        // Visual bar
        let bar_width = (state.brightness_level as u32) * 10;
        let bar_x = 14;
        let bar_y = 38;

        // Bar fill (skip outline for simplicity - just show fill)
        if bar_width > 0 {
            display.fill_rect(bar_x, bar_y, bar_width, 10);
        }

        // Instructions
        display.text("Sel: cycle  Nxt: exit", 4, 58);
    }
}
