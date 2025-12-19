//! Info view - displays string configurations.

use core::fmt::Write;
use heapless::String;

use crate::display::Display;
use crate::state::{AppState, NUM_STRINGS};

pub struct InfoView;

impl InfoView {
    pub fn new() -> Self {
        Self
    }

    pub fn render_to<D: Display>(&mut self, display: &mut D, state: &AppState) {
        display.text("String Info", 28, 10);

        // Show 5 items at a time
        let scroll = state.info_view.scroll_index as usize;
        let start_idx = if scroll >= 4 { scroll - 3 } else { 0 };

        for (i, idx) in (start_idx..).take(5).enumerate() {
            let y = 22 + (i as i32 * 9);
            let is_selected = idx == scroll;
            let prefix = if is_selected { "> " } else { "  " };

            let mut line: String<32> = String::new();

            if idx < NUM_STRINGS {
                let config = &state.strings[idx];
                write!(
                    line,
                    "{}S{:02}: {}px {}",
                    prefix,
                    idx,
                    config.pixel_count,
                    config.color_order.as_str()
                )
                .ok();
            } else if idx == NUM_STRINGS {
                write!(line, "{}[Exit]", prefix).ok();
            } else {
                break;
            }

            display.text(&line, 4, y);
        }
    }
}
