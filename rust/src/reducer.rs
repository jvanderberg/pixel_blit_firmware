//! Reducer for Pixel Blit firmware.
//!
//! Pure state transitions - no side effects allowed.
//! Same inputs always produce same outputs.

use crate::action::Action;
use crate::state::*;
use reducto::{changed, unchanged, Outcome, Reducer};

pub struct AppReducer;

impl Reducer for AppReducer {
    type State = AppState;
    type Action = Action;

    fn reduce(state: Self::State, action: Self::Action) -> Outcome<Self::State> {
        // If powered off, only respond to power-on actions
        if !state.is_powered_on {
            return match action {
                Action::PowerToggle | Action::ButtonSelect | Action::ButtonNext => {
                    changed(AppState {
                        is_powered_on: true,
                        ..state
                    })
                }
                _ => unchanged(state),
            };
        }

        match action {
            Action::Boot => {
                // Just trigger initial render
                changed(state)
            }

            Action::ButtonNext | Action::ButtonSelect
                if state.boot_state == BootState::Error =>
            {
                // Button press on error screen - continue to main menu
                changed(AppState {
                    boot_state: BootState::Ready,
                    ..state
                })
            }

            Action::ButtonNext => reduce_button_next(state),
            Action::ButtonSelect => reduce_button_select(state),

            Action::Tick1s => changed(AppState {
                uptime_seconds: state.uptime_seconds.wrapping_add(1),
                ..state
            }),

            Action::PowerToggle => {
                // Power off: stop all tests, return to main menu
                changed(AppState {
                    is_powered_on: false,
                    in_detail_view: false,
                    rainbow_test: RainbowTestState {
                        run_state: RunState::Stopped,
                        ..state.rainbow_test
                    },
                    string_length: StringLengthState {
                        run_state: RunState::Stopped,
                        ..state.string_length
                    },
                    ..state
                })
            }

            Action::BrightnessUp => {
                let new_level = if state.brightness_level >= BRIGHTNESS_MAX {
                    BRIGHTNESS_MAX
                } else {
                    state.brightness_level + 1
                };
                changed(AppState {
                    brightness_level: new_level,
                    ..state
                })
            }

            Action::BrightnessDown => {
                let new_level = if state.brightness_level <= BRIGHTNESS_MIN {
                    BRIGHTNESS_MIN
                } else {
                    state.brightness_level - 1
                };
                changed(AppState {
                    brightness_level: new_level,
                    ..state
                })
            }

            Action::RainbowFrameComplete { fps } => changed(AppState {
                rainbow_test: RainbowTestState {
                    fps,
                    ..state.rainbow_test
                },
                ..state
            }),

            Action::SdRescan => {
                // Clear error, keep current state - actual scan happens in side effect
                changed(AppState {
                    sd_card: SdCardState {
                        error_message: None,
                        ..state.sd_card
                    },
                    ..state
                })
            }

            Action::SdFilesScanned { count } => changed(AppState {
                sd_card: SdCardState {
                    mounted: true,
                    file_count: count,
                    error_message: None,
                    ..state.sd_card
                },
                boot_state: BootState::Ready,
                ..state
            }),

            Action::SdConfigLoaded {
                strings,
                string_count: _,
                max_pixel_count: _,
            } => changed(AppState {
                strings,
                boot_state: BootState::Ready,
                ..state
            }),

            Action::SdCardError { message } => changed(AppState {
                sd_card: SdCardState {
                    mounted: false,
                    file_count: 0,
                    error_message: None,
                    ..state.sd_card
                },
                boot_state: BootState::Error,
                boot_error: Some(message),
                ..state
            }),

            Action::SdConfigError { message } => changed(AppState {
                boot_state: BootState::Error,
                boot_error: Some(message),
                ..state
            }),

            Action::AutoToggle => changed(AppState {
                sd_card: SdCardState {
                    auto_loop: !state.sd_card.auto_loop,
                    ..state.sd_card
                },
                ..state
            }),

            Action::FseqNext | Action::FseqLoopComplete => {
                // TODO: implement file skip
                unchanged(state)
            }
        }
    }
}

/// Handle Next button press
fn reduce_button_next(state: AppState) -> Outcome<AppState> {
    if !state.in_detail_view {
        // Main menu: cycle to next item
        changed(AppState {
            menu_selection: state.menu_selection.next(),
            ..state
        })
    } else {
        // In detail view: behavior depends on current menu
        match state.menu_selection {
            MenuEntry::Info => {
                // Scroll through strings + Exit option
                let max_scroll = NUM_STRINGS as u8; // 0-31 = strings, 32 = Exit
                let new_scroll = if state.info_view.scroll_index >= max_scroll {
                    0
                } else {
                    state.info_view.scroll_index + 1
                };
                changed(AppState {
                    info_view: InfoViewState {
                        scroll_index: new_scroll,
                    },
                    ..state
                })
            }

            MenuEntry::BoardAddress => {
                // Next exits detail view
                changed(AppState {
                    in_detail_view: false,
                    ..state
                })
            }

            MenuEntry::SdCard => {
                // Scroll through files + Main Menu option
                if state.sd_card.is_playing {
                    // Stop playback
                    changed(AppState {
                        sd_card: SdCardState {
                            is_playing: false,
                            ..state.sd_card
                        },
                        ..state
                    })
                } else {
                    // Scroll file list
                    let max_scroll = state.sd_card.file_count; // files + Main Menu
                    let new_scroll = if state.sd_card.scroll_index >= max_scroll {
                        0
                    } else {
                        state.sd_card.scroll_index + 1
                    };
                    changed(AppState {
                        sd_card: SdCardState {
                            scroll_index: new_scroll,
                            ..state.sd_card
                        },
                        ..state
                    })
                }
            }

            MenuEntry::RainbowTest => {
                // Next exits detail view
                changed(AppState {
                    in_detail_view: false,
                    rainbow_test: RainbowTestState {
                        run_state: RunState::Stopped,
                        ..state.rainbow_test
                    },
                    ..state
                })
            }

            MenuEntry::StringLength => {
                // Increment pixel position (wraps at MAX_PIXELS_PER_STRING)
                let new_pixel = if state.string_length.current_pixel >= MAX_PIXELS_PER_STRING {
                    1
                } else {
                    state.string_length.current_pixel + 1
                };
                changed(AppState {
                    string_length: StringLengthState {
                        current_pixel: new_pixel,
                        ..state.string_length
                    },
                    ..state
                })
            }

            MenuEntry::Brightness => {
                // Next exits detail view
                changed(AppState {
                    in_detail_view: false,
                    ..state
                })
            }

            MenuEntry::Shutdown => {
                // Next exits detail view (cancel shutdown)
                changed(AppState {
                    in_detail_view: false,
                    ..state
                })
            }
        }
    }
}

/// Handle Select button press
fn reduce_button_select(state: AppState) -> Outcome<AppState> {
    if !state.in_detail_view {
        // Main menu: enter detail view (or handle Shutdown specially)
        if state.menu_selection == MenuEntry::Shutdown {
            // Enter shutdown confirmation
            changed(AppState {
                in_detail_view: true,
                ..state
            })
        } else {
            // Entering detail view - set run_state for LED menus
            let rainbow_test = if state.menu_selection == MenuEntry::RainbowTest {
                RainbowTestState {
                    run_state: RunState::Running,
                    ..state.rainbow_test
                }
            } else {
                state.rainbow_test
            };
            let string_length = if state.menu_selection == MenuEntry::StringLength {
                StringLengthState {
                    run_state: RunState::Running,
                    ..state.string_length
                }
            } else {
                state.string_length
            };
            changed(AppState {
                in_detail_view: true,
                rainbow_test,
                string_length,
                ..state
            })
        }
    } else {
        // In detail view: behavior depends on current menu
        match state.menu_selection {
            MenuEntry::Info => {
                // Select on Exit option exits, otherwise no-op
                if state.info_view.scroll_index >= NUM_STRINGS as u8 {
                    changed(AppState {
                        in_detail_view: false,
                        info_view: InfoViewState { scroll_index: 0 },
                        ..state
                    })
                } else {
                    unchanged(state)
                }
            }

            MenuEntry::BoardAddress => {
                // Cycle board ID
                let new_id = if state.board_id >= BOARD_ID_MAX {
                    0
                } else {
                    state.board_id + 1
                };
                changed(AppState {
                    board_id: new_id,
                    ..state
                })
            }

            MenuEntry::SdCard => {
                if state.sd_card.is_playing {
                    // Stop playback
                    changed(AppState {
                        sd_card: SdCardState {
                            is_playing: false,
                            ..state.sd_card
                        },
                        ..state
                    })
                } else if state.sd_card.scroll_index >= state.sd_card.file_count {
                    // Selected "Main Menu" - exit
                    changed(AppState {
                        in_detail_view: false,
                        ..state
                    })
                } else {
                    // Selected a file - start playback
                    changed(AppState {
                        sd_card: SdCardState {
                            is_playing: true,
                            playing_index: state.sd_card.scroll_index,
                            ..state.sd_card
                        },
                        ..state
                    })
                }
            }

            MenuEntry::RainbowTest => {
                // Cycle to next string
                let new_string = if state.rainbow_test.current_string >= (NUM_STRINGS - 1) as u8 {
                    0
                } else {
                    state.rainbow_test.current_string + 1
                };
                // Also start running if stopped
                changed(AppState {
                    rainbow_test: RainbowTestState {
                        current_string: new_string,
                        run_state: RunState::Running,
                        ..state.rainbow_test
                    },
                    ..state
                })
            }

            MenuEntry::StringLength => {
                // Save current pixel as length for current string, advance to next
                let mut new_lengths = state.string_length.lengths;
                new_lengths[state.string_length.current_string as usize] =
                    state.string_length.current_pixel;

                let new_string = state.string_length.current_string + 1;

                if new_string >= NUM_STRINGS as u8 {
                    // Done with all strings, exit
                    changed(AppState {
                        in_detail_view: false,
                        string_length: StringLengthState {
                            lengths: new_lengths,
                            current_string: 0,
                            current_pixel: 1,
                            run_state: RunState::Stopped,
                        },
                        ..state
                    })
                } else {
                    // Move to next string
                    changed(AppState {
                        string_length: StringLengthState {
                            lengths: new_lengths,
                            current_string: new_string,
                            current_pixel: 1,
                            run_state: RunState::Running,
                        },
                        ..state
                    })
                }
            }

            MenuEntry::Brightness => {
                // Cycle brightness level
                let new_level = if state.brightness_level >= BRIGHTNESS_MAX {
                    BRIGHTNESS_MIN
                } else {
                    state.brightness_level + 1
                };
                changed(AppState {
                    brightness_level: new_level,
                    ..state
                })
            }

            MenuEntry::Shutdown => {
                // Confirm shutdown - power off
                changed(AppState {
                    is_powered_on: false,
                    in_detail_view: false,
                    ..state
                })
            }
        }
    }
}
