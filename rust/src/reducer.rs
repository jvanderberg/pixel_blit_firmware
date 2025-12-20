//! Reducer for Pixel Blit firmware.
//!
//! Pure state transitions with explicit effects.
//! State is mutated in place, effects signal side effects to main loop.

use crate::action::Action;
use crate::effect::AppEffect;
use crate::state::*;
use reducto::Reducer;

pub struct AppReducer;

impl Reducer for AppReducer {
    type State = AppState;
    type Action = Action;
    type Effect = AppEffect;

    fn reduce(state: &mut Self::State, action: Self::Action) -> Self::Effect {
        // If powered off, only respond to power-on actions
        if !state.is_powered_on {
            return match action {
                Action::PowerToggle | Action::ButtonSelect | Action::ButtonNext => {
                    state.is_powered_on = true;
                    AppEffect::Render
                }
                _ => AppEffect::Unchanged,
            };
        }

        match action {
            Action::Boot => {
                // Just trigger initial render
                AppEffect::Render
            }

            Action::ButtonNext | Action::ButtonSelect
                if state.boot_state == BootState::Error =>
            {
                // Button press on error screen - continue to main menu
                state.boot_state = BootState::Ready;
                AppEffect::Render
            }

            Action::ButtonNext => reduce_button_next(state),
            Action::ButtonSelect => reduce_button_select(state),

            Action::Tick1s => {
                state.uptime_seconds = state.uptime_seconds.wrapping_add(1);
                AppEffect::Render
            }

            Action::PowerToggle => {
                // Power off: stop all tests, return to main menu
                state.is_powered_on = false;
                state.in_detail_view = false;
                state.rainbow_test.run_state = RunState::Stopped;
                state.string_length.run_state = RunState::Stopped;
                AppEffect::Render
            }

            Action::BrightnessUp => {
                if state.brightness_level >= BRIGHTNESS_MAX {
                    AppEffect::Unchanged
                } else {
                    state.brightness_level += 1;
                    AppEffect::Save
                }
            }

            Action::BrightnessDown => {
                if state.brightness_level <= BRIGHTNESS_MIN {
                    AppEffect::Unchanged
                } else {
                    state.brightness_level -= 1;
                    AppEffect::Save
                }
            }

            Action::RainbowFrameComplete { fps } => {
                state.rainbow_test.fps = fps;
                AppEffect::Render
            }

            Action::SdRescan => {
                // Clear error, keep current state - actual scan happens via effect
                state.sd_card.error_message = None;
                AppEffect::SdRescan
            }

            Action::SdFilesScanned { count } => {
                state.sd_card.mounted = true;
                state.sd_card.file_count = count;
                state.sd_card.error_message = None;
                state.boot_state = BootState::Ready;
                AppEffect::Render
            }

            Action::SdConfigLoaded {
                strings,
                string_count: _,
                max_pixel_count: _,
            } => {
                state.strings = strings;
                state.boot_state = BootState::Ready;
                AppEffect::Render
            }

            Action::SdCardError { message } => {
                state.sd_card.mounted = false;
                state.sd_card.file_count = 0;
                state.sd_card.error_message = None;
                state.boot_state = BootState::Error;
                state.boot_error = Some(message);
                AppEffect::Render
            }

            Action::SdConfigError { message } => {
                state.boot_state = BootState::Error;
                state.boot_error = Some(message);
                AppEffect::Render
            }

            Action::AutoToggle => {
                state.sd_card.auto_loop = !state.sd_card.auto_loop;
                AppEffect::Render
            }

            Action::FseqNext | Action::FseqLoopComplete => {
                // TODO: implement file skip
                AppEffect::Unchanged
            }
        }
    }
}

/// Handle Next button press
fn reduce_button_next(state: &mut AppState) -> AppEffect {
    if !state.in_detail_view {
        // Main menu: cycle to next item
        state.menu_selection = state.menu_selection.next();
        AppEffect::Render
    } else {
        // In detail view: behavior depends on current menu
        match state.menu_selection {
            MenuEntry::Info => {
                // Scroll through strings + Exit option
                let max_scroll = NUM_STRINGS as u8; // 0-31 = strings, 32 = Exit
                state.info_view.scroll_index = if state.info_view.scroll_index >= max_scroll {
                    0
                } else {
                    state.info_view.scroll_index + 1
                };
                AppEffect::Render
            }

            MenuEntry::BoardAddress => {
                // Next exits detail view
                state.in_detail_view = false;
                AppEffect::Render
            }

            MenuEntry::SdCard => {
                if state.sd_card.is_playing {
                    // Stop playback
                    state.sd_card.is_playing = false;
                    AppEffect::Render
                } else {
                    // Scroll file list
                    let max_scroll = state.sd_card.file_count; // files + Main Menu
                    state.sd_card.scroll_index = if state.sd_card.scroll_index >= max_scroll {
                        0
                    } else {
                        state.sd_card.scroll_index + 1
                    };
                    AppEffect::Render
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

            MenuEntry::Brightness => {
                // Next exits detail view
                state.in_detail_view = false;
                AppEffect::Render
            }

            MenuEntry::Shutdown => {
                // Next exits detail view (cancel shutdown)
                state.in_detail_view = false;
                AppEffect::Render
            }
        }
    }
}

/// Handle Select button press
fn reduce_button_select(state: &mut AppState) -> AppEffect {
    if !state.in_detail_view {
        // Main menu: enter detail view
        state.in_detail_view = true;

        match state.menu_selection {
            MenuEntry::RainbowTest => {
                state.rainbow_test.run_state = RunState::Running;
                AppEffect::RainbowStart
            }
            MenuEntry::StringLength => {
                state.string_length.run_state = RunState::Running;
                AppEffect::StringLengthStart
            }
            MenuEntry::SdCard => {
                // Entering SD card menu triggers rescan
                AppEffect::SdRescan
            }
            _ => AppEffect::Render,
        }
    } else {
        // In detail view: behavior depends on current menu
        match state.menu_selection {
            MenuEntry::Info => {
                // Select on Exit option exits, otherwise no-op
                if state.info_view.scroll_index >= NUM_STRINGS as u8 {
                    state.in_detail_view = false;
                    state.info_view.scroll_index = 0;
                    AppEffect::Render
                } else {
                    AppEffect::Unchanged
                }
            }

            MenuEntry::BoardAddress => {
                // Cycle board ID
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
                    AppEffect::Render
                } else if state.sd_card.scroll_index >= state.sd_card.file_count {
                    // Selected "Main Menu" - exit
                    state.in_detail_view = false;
                    AppEffect::Render
                } else {
                    // Selected a file - start playback
                    state.sd_card.is_playing = true;
                    state.sd_card.playing_index = state.sd_card.scroll_index;
                    AppEffect::Render
                }
            }

            MenuEntry::RainbowTest => {
                // Cycle to next string
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
                // Save current pixel as length for current string, advance to next
                state.string_length.lengths[state.string_length.current_string as usize] =
                    state.string_length.current_pixel;

                let new_string = state.string_length.current_string + 1;

                if new_string >= NUM_STRINGS as u8 {
                    // Done with all strings, exit
                    state.in_detail_view = false;
                    state.string_length.current_string = 0;
                    state.string_length.current_pixel = 1;
                    state.string_length.run_state = RunState::Stopped;
                    AppEffect::StringLengthStop
                } else {
                    // Move to next string
                    state.string_length.current_string = new_string;
                    state.string_length.current_pixel = 1;
                    AppEffect::StringLengthUpdate
                }
            }

            MenuEntry::Brightness => {
                // Cycle brightness level
                state.brightness_level = if state.brightness_level >= BRIGHTNESS_MAX {
                    BRIGHTNESS_MIN
                } else {
                    state.brightness_level + 1
                };
                AppEffect::Save
            }

            MenuEntry::Shutdown => {
                // Confirm shutdown - power off
                state.is_powered_on = false;
                state.in_detail_view = false;
                AppEffect::Render
            }
        }
    }
}
