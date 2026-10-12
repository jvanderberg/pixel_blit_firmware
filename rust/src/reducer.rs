//! Reducer for Pixel Blit firmware.
//!
//! Pure state transitions with explicit effects.
//! State is mutated in place, effects signal side effects to main loop.

use crate::action::Action;
use crate::effect::AppEffect;
use crate::state::*;
use reducto::{Effect, Reducer};

pub struct AppReducer;

impl Reducer for AppReducer {
    type State = AppState;
    type Action = Action;
    type Effect = AppEffect;

    fn reduce(state: &mut Self::State, action: Self::Action) -> Self::Effect {
        match action {
            Action::Boot => {
                if !state.is_powered_on { return AppEffect::Unchanged; }
                // Just trigger initial render
                AppEffect::changed()
            }

            Action::PowerToggle => {
                if !state.is_powered_on {
                    state.is_powered_on = true;
                } else {
                    // Power off: stop all tests, return to main menu
                    state.is_powered_on = false;
                    state.in_detail_view = false;
                    state.rainbow_test.run_state = RunState::Stopped;
                    state.string_length.run_state = RunState::Stopped;
                }
                AppEffect::changed()
            }

            Action::ButtonNext => {
                if !state.is_powered_on {
                    state.is_powered_on = true;
                    AppEffect::changed()
                } else if state.boot_state == BootState::Error {
                    state.boot_state = BootState::Ready;
                    AppEffect::changed()
                } else {
                    reduce_button_next(state)
                }
            }

            Action::ButtonSelect => {
                if !state.is_powered_on {
                    state.is_powered_on = true;
                    AppEffect::changed()
                } else if state.boot_state == BootState::Error {
                    state.boot_state = BootState::Ready;
                    AppEffect::changed()
                } else {
                    reduce_button_select(state)
                }
            }

            Action::Tick1s => {
                if !state.is_powered_on { return AppEffect::Unchanged; }
                state.uptime_seconds = state.uptime_seconds.wrapping_add(1);
                AppEffect::changed()
            }

            Action::BrightnessUp => {
                if !state.is_powered_on { return AppEffect::Unchanged; }
                if state.brightness_level >= BRIGHTNESS_MAX {
                    AppEffect::Unchanged
                } else {
                    state.brightness_level += 1;
                    AppEffect::Save
                }
            }

            Action::BrightnessDown => {
                if !state.is_powered_on { return AppEffect::Unchanged; }
                if state.brightness_level <= BRIGHTNESS_MIN {
                    AppEffect::Unchanged
                } else {
                    state.brightness_level -= 1;
                    AppEffect::Save
                }
            }

            Action::RainbowFrameComplete { fps } => {
                if !state.is_powered_on { return AppEffect::Unchanged; }
                state.rainbow_test.fps = fps;
                AppEffect::changed()
            }

            Action::SdRescan => {
                if !state.is_powered_on { return AppEffect::Unchanged; }
                state.sd_card.error_message = None;
                AppEffect::SdRescan
            }

            Action::SdFilesScanned { count } => {
                if !state.is_powered_on { return AppEffect::Unchanged; }
                state.sd_card.mounted = true;
                state.sd_card.file_count = count;
                state.sd_card.error_message = None;
                state.boot_state = BootState::Ready;
                AppEffect::changed()
            }

            Action::SdConfigLoaded { strings, string_count: _, max_pixel_count: _ } => {
                if !state.is_powered_on { return AppEffect::Unchanged; }
                state.strings = strings;
                state.boot_state = BootState::Ready;
                AppEffect::changed()
            }

            Action::SdCardError { message } => {
                if !state.is_powered_on { return AppEffect::Unchanged; }
                state.sd_card.mounted = false;
                state.sd_card.file_count = 0;
                state.sd_card.error_message = None;
                state.boot_state = BootState::Error;
                state.boot_error = Some(message);
                AppEffect::changed()
            }

            Action::SdConfigError { message } => {
                if !state.is_powered_on { return AppEffect::Unchanged; }
                state.boot_state = BootState::Error;
                state.boot_error = Some(message);
                AppEffect::changed()
            }

            Action::AutoToggle => {
                if !state.is_powered_on { return AppEffect::Unchanged; }
                state.sd_card.auto_loop = !state.sd_card.auto_loop;
                AppEffect::changed()
            }

            Action::FseqNext => {
                if !state.is_powered_on { return AppEffect::Unchanged; }
                if !state.sd_card.is_playing { return AppEffect::Unchanged; }

                // Advance to next file (wrap around)
                let next_index = if state.sd_card.playing_index + 1 >= state.sd_card.file_count {
                    0
                } else {
                    state.sd_card.playing_index + 1
                };
                state.sd_card.playing_index = next_index;
                state.sd_card.scroll_index = next_index;
                AppEffect::FseqStart
            }

            Action::FseqLoopComplete => {
                if !state.is_powered_on { return AppEffect::Unchanged; }
                if !state.sd_card.is_playing { return AppEffect::Unchanged; }

                if state.sd_card.auto_loop {
                    // Auto-advance to next file
                    let next_index = if state.sd_card.playing_index + 1 >= state.sd_card.file_count {
                        0
                    } else {
                        state.sd_card.playing_index + 1
                    };
                    state.sd_card.playing_index = next_index;
                    state.sd_card.scroll_index = next_index;
                    AppEffect::FseqStart
                } else {
                    // Just loop current file (no state change, but re-trigger)
                    AppEffect::Unchanged
                }
            }

            Action::FseqFpsUpdate { fps } => {
                if !state.is_powered_on { return AppEffect::Unchanged; }
                state.sd_card.fps = fps;
                AppEffect::changed()
            }
        }
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
                AppEffect::FseqStop
            } else {
                let max_scroll = state.sd_card.file_count;
                state.sd_card.scroll_index = if state.sd_card.scroll_index >= max_scroll {
                    0
                } else {
                    state.sd_card.scroll_index + 1
                };
                AppEffect::changed()
            }
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
                // Stop playback
                state.sd_card.is_playing = false;
                AppEffect::FseqStop
            } else if state.sd_card.scroll_index >= state.sd_card.file_count {
                // Exit option selected
                state.in_detail_view = false;
                AppEffect::changed()
            } else {
                // Start playback of selected file
                state.sd_card.is_playing = true;
                state.sd_card.playing_index = state.sd_card.scroll_index;
                AppEffect::FseqStart
            }
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
