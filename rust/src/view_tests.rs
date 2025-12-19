//! View tests using mock display.

#[cfg(test)]
mod tests {
    use crate::display::{Display, MockDisplay};
    use crate::state::*;
    use crate::views::*;

    // =========================================================================
    // Main Menu View
    // =========================================================================

    #[test]
    fn main_menu_shows_title() {
        let mut display = MockDisplay::new();
        let mut view = MainMenuView::new();
        let state = AppState::default();

        view.render_to(&mut display, &state);

        assert!(display.contains("Pixel Blit"));
    }

    #[test]
    fn main_menu_shows_selected_item() {
        let mut display = MockDisplay::new();
        let mut view = MainMenuView::new();

        // Select Brightness menu
        let state = AppState {
            menu_selection: MenuEntry::Brightness,
            ..AppState::default()
        };
        view.render_to(&mut display, &state);

        assert!(display.contains("> Brightness"));
    }

    #[test]
    fn main_menu_shows_all_entries() {
        let mut display = MockDisplay::new();
        let mut view = MainMenuView::new();

        for entry in [
            MenuEntry::Info,
            MenuEntry::BoardAddress,
            MenuEntry::SdCard,
            MenuEntry::RainbowTest,
            MenuEntry::StringLength,
            MenuEntry::Brightness,
            MenuEntry::Shutdown,
        ] {
            display.clear_buffer();
            let state = AppState {
                menu_selection: entry,
                ..AppState::default()
            };
            view.render_to(&mut display, &state);
            assert!(
                display.contains(entry.label()),
                "Menu should show '{}'",
                entry.label()
            );
        }
    }

    // =========================================================================
    // Info View
    // =========================================================================

    #[test]
    fn info_view_shows_title() {
        let mut display = MockDisplay::new();
        let mut view = InfoView::new();
        let state = AppState::default();

        view.render_to(&mut display, &state);
        assert!(display.contains("String Info"));
    }

    #[test]
    fn info_view_shows_string_config() {
        let mut display = MockDisplay::new();
        let mut view = InfoView::new();
        let state = AppState::default();

        view.render_to(&mut display, &state);
        // Should show at least one string entry with format "S00: Npx ORDER"
        assert!(display.contains("S00:"));
    }

    #[test]
    fn info_view_shows_exit_option() {
        let mut display = MockDisplay::new();
        let mut view = InfoView::new();

        // Scroll to exit option (after all strings)
        let state = AppState {
            info_view: InfoViewState {
                scroll_index: NUM_STRINGS as u8,
            },
            ..AppState::default()
        };
        view.render_to(&mut display, &state);
        assert!(display.contains("[Exit]"));
    }

    // =========================================================================
    // Board Address View
    // =========================================================================

    #[test]
    fn board_address_shows_title() {
        let mut display = MockDisplay::new();
        let mut view = BoardAddressView::new();
        let state = AppState::default();

        view.render_to(&mut display, &state);
        assert!(display.contains("Board Address"));
    }

    #[test]
    fn board_address_shows_current_id() {
        let mut display = MockDisplay::new();
        let mut view = BoardAddressView::new();

        let state = AppState {
            board_id: 5,
            ..AppState::default()
        };
        view.render_to(&mut display, &state);
        assert!(display.contains("Board ID: 5"));
    }

    #[test]
    fn board_address_shows_all_ids() {
        let mut display = MockDisplay::new();
        let mut view = BoardAddressView::new();

        for id in 0..=BOARD_ID_MAX {
            display.clear_buffer();
            let state = AppState {
                board_id: id,
                ..AppState::default()
            };
            view.render_to(&mut display, &state);
            let expected = format!("Board ID: {}", id);
            assert!(
                display.contains(&expected),
                "Should show '{}'",
                expected
            );
        }
    }

    // =========================================================================
    // Brightness View
    // =========================================================================

    #[test]
    fn brightness_shows_title() {
        let mut display = MockDisplay::new();
        let mut view = BrightnessView::new();
        let state = AppState::default();

        view.render_to(&mut display, &state);
        assert!(display.contains("Brightness"));
    }

    #[test]
    fn brightness_shows_level() {
        let mut display = MockDisplay::new();
        let mut view = BrightnessView::new();

        let state = AppState {
            brightness_level: 7,
            ..AppState::default()
        };
        view.render_to(&mut display, &state);
        assert!(display.contains("Level: 7"));
    }

    #[test]
    fn brightness_bar_grows_with_level() {
        let mut display = MockDisplay::new();
        let mut view = BrightnessView::new();

        // Render at min brightness
        let state_min = AppState {
            brightness_level: BRIGHTNESS_MIN,
            ..AppState::default()
        };
        view.render_to(&mut display, &state_min);
        let pixels_min = display.count_pixels_in_region(14, 38, 100, 10);

        // Render at max brightness
        display.clear_buffer();
        let state_max = AppState {
            brightness_level: BRIGHTNESS_MAX,
            ..AppState::default()
        };
        view.render_to(&mut display, &state_max);
        let pixels_max = display.count_pixels_in_region(14, 38, 100, 10);

        // Max should have more pixels in the bar region
        assert!(pixels_max > pixels_min, "Max brightness bar should be larger");
    }

    // =========================================================================
    // Rainbow Test View
    // =========================================================================

    #[test]
    fn rainbow_test_shows_title() {
        let mut display = MockDisplay::new();
        let mut view = RainbowTestView::new();
        let state = AppState::default();

        view.render_to(&mut display, &state);
        assert!(display.contains("Rainbow Test"));
    }

    #[test]
    fn rainbow_test_shows_running_state() {
        let mut display = MockDisplay::new();
        let mut view = RainbowTestView::new();

        let state = AppState {
            rainbow_test: RainbowTestState {
                run_state: RunState::Running,
                ..RainbowTestState::default()
            },
            ..AppState::default()
        };
        view.render_to(&mut display, &state);
        assert!(display.contains("RUNNING"));
    }

    #[test]
    fn rainbow_test_shows_stopped_state() {
        let mut display = MockDisplay::new();
        let mut view = RainbowTestView::new();

        let state = AppState {
            rainbow_test: RainbowTestState {
                run_state: RunState::Stopped,
                ..RainbowTestState::default()
            },
            ..AppState::default()
        };
        view.render_to(&mut display, &state);
        assert!(display.contains("STOPPED"));
    }

    // =========================================================================
    // String Length View
    // =========================================================================

    #[test]
    fn string_length_shows_title() {
        let mut display = MockDisplay::new();
        let mut view = StringLengthView::new();
        let state = AppState::default();

        view.render_to(&mut display, &state);
        assert!(display.contains("String Length"));
    }

    #[test]
    fn string_length_shows_current_string() {
        let mut display = MockDisplay::new();
        let mut view = StringLengthView::new();

        let state = AppState {
            string_length: StringLengthState {
                current_string: 5,
                current_pixel: 100,
                ..StringLengthState::default()
            },
            ..AppState::default()
        };
        view.render_to(&mut display, &state);
        assert!(display.contains("String: 5"));
        assert!(display.contains("Pixel: 100"));
    }

    // =========================================================================
    // SD Card View
    // =========================================================================

    #[test]
    fn sd_card_shows_title() {
        let mut display = MockDisplay::new();
        let mut view = SdCardView::new();
        let state = AppState::default();

        view.render_to(&mut display, &state);
        assert!(display.contains("SD Card"));
    }

    #[test]
    fn sd_card_shows_unmounted() {
        let mut display = MockDisplay::new();
        let mut view = SdCardView::new();

        let state = AppState {
            sd_card: SdCardState {
                mounted: false,
                ..SdCardState::default()
            },
            ..AppState::default()
        };
        view.render_to(&mut display, &state);
        assert!(display.contains("Scanning"));
    }

    #[test]
    fn sd_card_shows_no_files() {
        let mut display = MockDisplay::new();
        let mut view = SdCardView::new();

        let state = AppState {
            sd_card: SdCardState {
                mounted: true,
                file_count: 0,
                ..SdCardState::default()
            },
            ..AppState::default()
        };
        view.render_to(&mut display, &state);
        assert!(display.contains("No .fseq files"));
    }

    #[test]
    fn sd_card_shows_file_list() {
        let mut display = MockDisplay::new();
        let mut view = SdCardView::new();

        // In test mode, filenames come from get_filename() which returns "file0.fseq", "file1.fseq", etc.
        let state = AppState {
            sd_card: SdCardState {
                mounted: true,
                file_count: 2,
                scroll_index: 0,
                ..SdCardState::default()
            },
            ..AppState::default()
        };
        view.render_to(&mut display, &state);
        // Shows scrollable list with placeholder filenames and Exit option
        assert!(display.contains("> file0.fseq")); // First file selected
        assert!(display.contains("file1.fseq")); // Second file visible
        assert!(display.contains("[Exit]")); // Exit option visible
    }

    #[test]
    fn sd_card_shows_playing() {
        let mut display = MockDisplay::new();
        let mut view = SdCardView::new();

        // In test mode, filenames come from get_filename() which returns "file3.fseq"
        let state = AppState {
            sd_card: SdCardState {
                mounted: true,
                file_count: 10,
                is_playing: true,
                playing_index: 3,
                auto_loop: false,
                ..SdCardState::default()
            },
            ..AppState::default()
        };
        view.render_to(&mut display, &state);
        assert!(display.contains("PLAYING"));
        assert!(display.contains("file3.fseq"));
    }

    #[test]
    fn sd_card_shows_auto_loop() {
        let mut display = MockDisplay::new();
        let mut view = SdCardView::new();

        let state = AppState {
            sd_card: SdCardState {
                mounted: true,
                file_count: 10,
                is_playing: true,
                playing_index: 0,
                auto_loop: true,
                ..SdCardState::default()
            },
            ..AppState::default()
        };
        view.render_to(&mut display, &state);
        assert!(display.contains("PLAYING [AUTO]"));
    }

    // =========================================================================
    // Shutdown View
    // =========================================================================

    #[test]
    fn shutdown_shows_confirmation() {
        let mut display = MockDisplay::new();
        let mut view = ShutdownView::new();
        let state = AppState::default();

        view.render_to(&mut display, &state);
        assert!(display.contains("Shutdown"));
        assert!(display.contains("Power off?"));
        assert!(display.contains("Sel: confirm"));
        assert!(display.contains("Nxt: cancel"));
    }

    // =========================================================================
    // Root View
    // =========================================================================

    #[test]
    fn root_view_renders_powered_off_blank() {
        let display = MockDisplay::new();
        let mut view = RootView::new(display);

        let state = AppState {
            is_powered_on: false,
            ..AppState::default()
        };

        use reducto::View;
        view.render(&state);
        // Powered off should be blank - no panic is the main check
    }

    #[test]
    fn root_view_renders_main_menu() {
        let display = MockDisplay::new();
        let mut view = RootView::new(display);

        let state = AppState {
            is_powered_on: true,
            in_detail_view: false,
            ..AppState::default()
        };

        use reducto::View;
        view.render(&state);
    }

    #[test]
    fn root_view_dispatches_to_detail_views() {
        use reducto::View;

        for entry in [
            MenuEntry::Info,
            MenuEntry::BoardAddress,
            MenuEntry::SdCard,
            MenuEntry::RainbowTest,
            MenuEntry::StringLength,
            MenuEntry::Brightness,
            MenuEntry::Shutdown,
        ] {
            let display = MockDisplay::new();
            let mut view = RootView::new(display);

            let state = AppState {
                is_powered_on: true,
                in_detail_view: true,
                menu_selection: entry,
                ..AppState::default()
            };
            view.render(&state);
        }
    }
}
