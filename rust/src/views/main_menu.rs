//! Main menu view - displays the list of menu items.

use core::fmt::Write;
use heapless::String;

use crate::display::Display;
use crate::state::{AppState, MenuEntry};

pub struct MainMenuView;

impl MainMenuView {
    pub fn new() -> Self {
        Self
    }

    pub fn render_to<D: Display>(&mut self, display: &mut D, state: &AppState) {
        // Title
        display.text("Pixel Blit", 28, 10);

        // Menu items (show 5 at a time with scrolling window)
        let selected_idx = state.menu_selection.index();
        let start_idx = if selected_idx >= 4 {
            selected_idx - 3
        } else {
            0
        };

        for (i, menu_idx) in (start_idx..).take(5).enumerate() {
            if menu_idx >= MenuEntry::COUNT {
                break;
            }

            let menu_entry = match menu_idx {
                0 => MenuEntry::Info,
                1 => MenuEntry::BoardAddress,
                2 => MenuEntry::SdCard,
                3 => MenuEntry::RainbowTest,
                4 => MenuEntry::StringLength,
                5 => MenuEntry::Brightness,
                6 => MenuEntry::Shutdown,
                _ => break,
            };

            let y = 22 + (i as i32 * 9);
            let prefix = if menu_idx == selected_idx { "> " } else { "  " };

            let mut line: String<32> = String::new();
            write!(line, "{}{}", prefix, menu_entry.label()).ok();

            display.text(&line, 4, y);
        }
    }
}
