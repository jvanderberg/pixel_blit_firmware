//! SD card view - file browser and playback.

use core::fmt::Write;
use heapless::String;

use crate::display::Display;
use crate::state::AppState;

/// Number of visible items in the file list
const VISIBLE_ITEMS: usize = 5;

/// Get filename - uses static buffer on firmware, placeholder on tests
#[cfg(feature = "firmware")]
fn get_filename(index: usize) -> String<32> {
    crate::sdcard::get_filename(index)
}

#[cfg(not(feature = "firmware"))]
fn get_filename(index: usize) -> String<32> {
    let mut s = String::new();
    write!(s, "file{}.fseq", index).ok();
    s
}

pub struct SdCardView;

impl SdCardView {
    pub fn render<D: Display>(display: &mut D, state: &AppState) {
        display.text("SD Card", 40, 10);

        // Show error message if present
        if let Some(error) = state.sd_card.error_message {
            display.text(error, 28, 30);
            display.text("Check SD card", 22, 42);
            return;
        }

        if !state.sd_card.mounted {
            display.text("Scanning...", 28, 30);
        } else if state.sd_card.is_playing {
            // Show FPS in header
            let mut header: String<24> = String::new();
            if state.sd_card.fps > 0 {
                write!(header, "PLAYING @ {}fps", state.sd_card.fps).ok();
            } else {
                write!(header, "PLAYING...").ok();
            }
            let x = if header.len() > 20 { 0 } else { (128 - header.len() * 6) / 2 };
            display.text(&header, x as i32, 20);

            // Show auto mode indicator
            if state.sd_card.auto_loop {
                display.text("[AUTO]", 44, 32);
            }

            // Show playing filename
            let idx = state.sd_card.playing_index as usize;
            let name = get_filename(idx);
            // Center the filename (roughly)
            let x = if name.len() > 20 { 0 } else { (128 - name.len() * 6) / 2 };
            display.text(&name, x as i32, 44);
            display.text("Press to stop", 22, 56);
        } else if state.sd_card.file_count == 0 {
            display.text("No .fseq files", 20, 30);
            display.text("found", 48, 42);
        } else {
            // Scrollable file list
            let selected = state.sd_card.scroll_index as usize;
            let total_items = state.sd_card.file_count as usize + 1; // +1 for Exit option

            // Calculate scroll window - keep selected item visible
            let window_start = if selected < VISIBLE_ITEMS {
                0
            } else if selected >= total_items.saturating_sub(VISIBLE_ITEMS) {
                total_items.saturating_sub(VISIBLE_ITEMS)
            } else {
                selected.saturating_sub(1) // Keep selected item near middle
            };

            // Render visible items
            for i in 0..VISIBLE_ITEMS {
                let item_index = window_start + i;
                if item_index >= total_items {
                    break;
                }

                let y = 20 + (i as i32 * 9);
                let is_selected = item_index == selected;
                let prefix = if is_selected { ">" } else { " " };

                if item_index >= state.sd_card.file_count as usize {
                    // Exit option
                    let mut line: String<24> = String::new();
                    write!(line, "{} [Exit]", prefix).ok();
                    display.text(&line, 4, y);
                } else {
                    // File entry
                    let name = get_filename(item_index);
                    let mut line: String<24> = String::new();
                    write!(line, "{} {}", prefix, name).ok();
                    // Truncate if too long (20 chars max for display)
                    if line.len() > 20 {
                        line.truncate(17);
                        line.push_str("...").ok();
                    }
                    display.text(&line, 4, y);
                }
            }

            // Show scroll indicator if list is longer than visible
            if total_items > VISIBLE_ITEMS {
                let mut indicator: String<8> = String::new();
                write!(indicator, "{}/{}", selected + 1, total_items).ok();
                display.text(&indicator, 100, 10);
            }
        }
    }
}
