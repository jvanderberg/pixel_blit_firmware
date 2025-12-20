//! Reducer for Pixel Blit firmware.
//!
//! Pure state transitions with explicit effects.
//! State is mutated in place, effects signal side effects to main loop.

use crate::action::Action;
use crate::effect::AppEffect;
use crate::state::*;
use reducto::Effect;

reducto::reducer! {
    pub AppReducer for AppState, Action, AppEffect {
        Action::Boot => |state| {
            if !state.is_powered_on { return AppEffect::Unchanged; }
            // Just trigger initial render
        },

        Action::PowerToggle => |state| {
            if !state.is_powered_on {
                state.is_powered_on = true
            } else {
                // Power off: stop all tests, return to main menu
                state.is_powered_on = false;
                state.in_detail_view = false;
                state.rainbow_test.run_state = RunState::Stopped;
                state.string_length.run_state = RunState::Stopped
            }
        },

        Action::ButtonNext => |state| {
            if !state.is_powered_on {
                state.is_powered_on = true
            } else if state.boot_state == BootState::Error {
                state.boot_state = BootState::Ready
            } else {
                return reduce_button_next(state);
            }
        },

        Action::ButtonSelect => |state| {
            if !state.is_powered_on {
                state.is_powered_on = true
            } else if state.boot_state == BootState::Error {
                state.boot_state = BootState::Ready
            } else {
                return reduce_button_select(state);
            }
        },

        Action::Tick1s => |state| {
            if !state.is_powered_on { return AppEffect::Unchanged; }
            state.uptime_seconds = state.uptime_seconds.wrapping_add(1)
        },

        Action::BrightnessUp => |state| {
            if !state.is_powered_on { return AppEffect::Unchanged; }
            if state.brightness_level >= BRIGHTNESS_MAX {
                AppEffect::Unchanged
            } else {
                state.brightness_level += 1;
                AppEffect::Save
            }
        },

        Action::BrightnessDown => |state| {
            if !state.is_powered_on { return AppEffect::Unchanged; }
            if state.brightness_level <= BRIGHTNESS_MIN {
                AppEffect::Unchanged
            } else {
                state.brightness_level -= 1;
                AppEffect::Save
            }
        },

        Action::RainbowFrameComplete { fps } => |state| {
            if !state.is_powered_on { return AppEffect::Unchanged; }
            state.rainbow_test.fps = fps
        },

        Action::SdRescan => |state| {
            if !state.is_powered_on { return AppEffect::Unchanged; }
            state.sd_card.error_message = None;
            AppEffect::SdRescan
        },

        Action::SdFilesScanned { count } => |state| {
            if !state.is_powered_on { return AppEffect::Unchanged; }
            state.sd_card.mounted = true;
            state.sd_card.file_count = count;
            state.sd_card.error_message = None;
            state.boot_state = BootState::Ready
        },

        Action::SdConfigLoaded { strings, string_count: _, max_pixel_count: _ } => |state| {
            if !state.is_powered_on { return AppEffect::Unchanged; }
            state.strings = strings;
            state.boot_state = BootState::Ready
        },

        Action::SdCardError { message } => |state| {
            if !state.is_powered_on { return AppEffect::Unchanged; }
            state.sd_card.mounted = false;
            state.sd_card.file_count = 0;
            state.sd_card.error_message = None;
            state.boot_state = BootState::Error;
            state.boot_error = Some(message)
        },

        Action::SdConfigError { message } => |state| {
            if !state.is_powered_on { return AppEffect::Unchanged; }
            state.boot_state = BootState::Error;
            state.boot_error = Some(message)
        },

        Action::AutoToggle => |state| {
            if !state.is_powered_on { return AppEffect::Unchanged; }
            state.sd_card.auto_loop = !state.sd_card.auto_loop
        },

        Action::FseqNext | Action::FseqLoopComplete => |_state| {
            // TODO: implement file skip
            AppEffect::Unchanged
        },
    }
}

/// Handle Next button press
fn reduce_button_next(state: &mut AppState) -> AppEffect {
    if !state.in_detail_view {
        // Main menu: cycle to next item
        state.menu_selection = state.menu_selection.next();
        return AppEffect::changed();
    }

    // In detail view: behavior depends on current menu
    match state.menu_selection {
        MenuEntry::Info => {
            // Scroll through strings + Exit option
            let max_scroll = NUM_STRINGS as u8;
            state.info_view.scroll_index = if state.info_view.scroll_index >= max_scroll {
                0
            } else {
                state.info_view.scroll_index + 1
            };
            AppEffect::changed()
        }

        MenuEntry::BoardAddress | MenuEntry::Brightness | MenuEntry::Shutdown => {
            // Next exits detail view
            state.in_detail_view = false;
            AppEffect::changed()
        }

        MenuEntry::SdCard => {
            if state.sd_card.is_playing {
                state.sd_card.is_playing = false;
            } else {
                let max_scroll = state.sd_card.file_count;
                state.sd_card.scroll_index = if state.sd_card.scroll_index >= max_scroll {
                    0
                } else {
                    state.sd_card.scroll_index + 1
                };
            }
            AppEffect::changed()
        }

        MenuEntry::RainbowTest => {
            // Next exits detail view and stops rainbow
            state.in_detail_view = false;
            state.rainbow_test.run_state = RunState::Stopped;
            AppEffect::RainbowStop
        }

        MenuEntry::StringLength => {
            // Increment pixel position (wraps at MAX_PIXELS_PER_STRING)
            state.string_length.current_pixel =
                if state.string_length.current_pixel >= MAX_PIXELS_PER_STRING {
                    1
                } else {
                    state.string_length.current_pixel + 1
                };
            AppEffect::StringLengthUpdate
        }
    }
}

/// Handle Select button press
fn reduce_button_select(state: &mut AppState) -> AppEffect {
    if !state.in_detail_view {
        // Main menu: enter detail view
        state.in_detail_view = true;

        return match state.menu_selection {
            MenuEntry::RainbowTest => {
                state.rainbow_test.run_state = RunState::Running;
                AppEffect::RainbowStart
            }
            MenuEntry::StringLength => {
                state.string_length.run_state = RunState::Running;
                AppEffect::StringLengthStart
            }
            MenuEntry::SdCard => AppEffect::SdRescan,
            _ => AppEffect::changed(),
        };
    }

    // In detail view: behavior depends on current menu
    match state.menu_selection {
        MenuEntry::Info => {
            if state.info_view.scroll_index >= NUM_STRINGS as u8 {
                state.in_detail_view = false;
                state.info_view.scroll_index = 0;
                AppEffect::changed()
            } else {
                AppEffect::Unchanged
            }
        }

        MenuEntry::BoardAddress => {
            state.board_id = if state.board_id >= BOARD_ID_MAX {
                0
            } else {
                state.board_id + 1
            };
            AppEffect::Save
        }

        MenuEntry::SdCard => {
            if state.sd_card.is_playing {
                state.sd_card.is_playing = false;
            } else if state.sd_card.scroll_index >= state.sd_card.file_count {
                state.in_detail_view = false;
            } else {
                state.sd_card.is_playing = true;
                state.sd_card.playing_index = state.sd_card.scroll_index;
            }
            AppEffect::changed()
        }

        MenuEntry::RainbowTest => {
            state.rainbow_test.current_string =
                if state.rainbow_test.current_string >= (NUM_STRINGS - 1) as u8 {
                    0
                } else {
                    state.rainbow_test.current_string + 1
                };
            state.rainbow_test.run_state = RunState::Running;
            AppEffect::RainbowUpdate
        }

        MenuEntry::StringLength => {
            state.string_length.lengths[state.string_length.current_string as usize] =
                state.string_length.current_pixel;

            let new_string = state.string_length.current_string + 1;

            if new_string >= NUM_STRINGS as u8 {
                state.in_detail_view = false;
                state.string_length.current_string = 0;
                state.string_length.current_pixel = 1;
                state.string_length.run_state = RunState::Stopped;
                AppEffect::StringLengthStop
            } else {
                state.string_length.current_string = new_string;
                state.string_length.current_pixel = 1;
                AppEffect::StringLengthUpdate
            }
        }

        MenuEntry::Brightness => {
            state.brightness_level = if state.brightness_level >= BRIGHTNESS_MAX {
                BRIGHTNESS_MIN
            } else {
                state.brightness_level + 1
            };
            AppEffect::Save
        }

        MenuEntry::Shutdown => {
            state.is_powered_on = false;
            state.in_detail_view = false;
            AppEffect::changed()
        }
    }
}
