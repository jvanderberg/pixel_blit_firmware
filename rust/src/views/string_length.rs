//! String length view - measure/configure LED string lengths.

use core::fmt::Write;
use heapless::String;

use crate::display::Display;
use crate::state::AppState;

pub struct StringLengthView;

impl StringLengthView {
    pub fn new() -> Self {
        Self
    }

    pub fn render_to<D: Display>(&mut self, display: &mut D, state: &AppState) {
        display.text("String Length", 22, 10);

        let mut line: String<32> = String::new();
        write!(line, "String: {}", state.string_length.current_string).ok();
        display.text(&line, 34, 26);

        line.clear();
        write!(line, "Pixel: {}", state.string_length.current_pixel).ok();
        display.text(&line, 36, 36);

        let saved = state.string_length.lengths[state.string_length.current_string as usize];
        line.clear();
        write!(line, "Saved: {}", saved).ok();
        display.text(&line, 36, 46);

        display.text("Nxt:+1 Sel:save", 16, 58);
    }
}
