//! Tests for the reducer - run with `cargo test` on host.

#[cfg(test)]
mod tests {
    use crate::action::Action;
    use crate::effect::AppEffect;
    use crate::reducer::AppReducer;
    use crate::state::*;
    use reducto::Reducer;

    fn dispatch(state: &mut AppState, action: Action) -> AppEffect {
        AppReducer::reduce(state, action)
    }

    // =========================================================================
    // Boot
    // =========================================================================

    #[test]
    fn boot_triggers_render() {
        let mut state = AppState::default();
        let effect = dispatch(&mut state, Action::Boot);
        assert_eq!(effect, AppEffect::None);
    }

    // =========================================================================
    // Menu Navigation
    // =========================================================================

    #[test]
    fn next_cycles_menu_items() {
        let mut state = AppState::default();
        assert_eq!(state.menu_selection, MenuEntry::Info);

        dispatch(&mut state, Action::ButtonNext);
        assert_eq!(state.menu_selection, MenuEntry::BoardAddress);

        dispatch(&mut state, Action::ButtonNext);
        assert_eq!(state.menu_selection, MenuEntry::SdCard);

        dispatch(&mut state, Action::ButtonNext);
        assert_eq!(state.menu_selection, MenuEntry::RainbowTest);

        dispatch(&mut state, Action::ButtonNext);
        assert_eq!(state.menu_selection, MenuEntry::StringLength);

        dispatch(&mut state, Action::ButtonNext);
        assert_eq!(state.menu_selection, MenuEntry::Brightness);

        dispatch(&mut state, Action::ButtonNext);
        assert_eq!(state.menu_selection, MenuEntry::Shutdown);

        // Wraps back to Info
        dispatch(&mut state, Action::ButtonNext);
        assert_eq!(state.menu_selection, MenuEntry::Info);
    }

    #[test]
    fn select_enters_detail_view() {
        let mut state = AppState::default();
        assert!(!state.in_detail_view);

        dispatch(&mut state, Action::ButtonSelect);
        assert!(state.in_detail_view);
    }

    #[test]
    fn next_in_main_menu_does_not_enter_detail() {
        let mut state = AppState::default();
        dispatch(&mut state, Action::ButtonNext);
        assert!(!state.in_detail_view);
    }

    // =========================================================================
    // Board Address
    // =========================================================================

    #[test]
    fn board_address_select_cycles_id() {
        let mut state = AppState::default();
        state.menu_selection = MenuEntry::BoardAddress;
        state.in_detail_view = true;
        assert_eq!(state.board_id, 0);

        let effect = dispatch(&mut state, Action::ButtonSelect);
        assert_eq!(state.board_id, 1);
        assert_eq!(effect, AppEffect::Save); // Board ID change triggers save

        // Cycle to max
        for _ in 0..14 {
            dispatch(&mut state, Action::ButtonSelect);
        }
        assert_eq!(state.board_id, 15);

        // Wraps to 0
        dispatch(&mut state, Action::ButtonSelect);
        assert_eq!(state.board_id, 0);
    }

    #[test]
    fn board_address_next_exits() {
        let mut state = AppState::default();
        state.menu_selection = MenuEntry::BoardAddress;
        state.in_detail_view = true;

        dispatch(&mut state, Action::ButtonNext);
        assert!(!state.in_detail_view);
    }

    // =========================================================================
    // Brightness
    // =========================================================================

    #[test]
    fn brightness_select_cycles() {
        let mut state = AppState::default();
        state.menu_selection = MenuEntry::Brightness;
        state.in_detail_view = true;
        state.brightness_level = BRIGHTNESS_DEFAULT; // 10

        let effect = dispatch(&mut state, Action::ButtonSelect);
        assert_eq!(state.brightness_level, 1); // Wraps to min
        assert_eq!(effect, AppEffect::Save);

        dispatch(&mut state, Action::ButtonSelect);
        assert_eq!(state.brightness_level, 2);
    }

    #[test]
    fn brightness_up_down_from_ir() {
        let mut state = AppState::default();
        state.brightness_level = 5;

        let effect = dispatch(&mut state, Action::BrightnessUp);
        assert_eq!(state.brightness_level, 6);
        assert_eq!(effect, AppEffect::Save);

        let effect = dispatch(&mut state, Action::BrightnessDown);
        assert_eq!(state.brightness_level, 5);
        assert_eq!(effect, AppEffect::Save);
    }

    #[test]
    fn brightness_clamps_at_bounds() {
        let mut state = AppState::default();
        state.brightness_level = BRIGHTNESS_MAX;

        let effect = dispatch(&mut state, Action::BrightnessUp);
        assert_eq!(state.brightness_level, BRIGHTNESS_MAX);
        assert_eq!(effect, AppEffect::Unchanged);

        state.brightness_level = BRIGHTNESS_MIN;
        let effect = dispatch(&mut state, Action::BrightnessDown);
        assert_eq!(state.brightness_level, BRIGHTNESS_MIN);
        assert_eq!(effect, AppEffect::Unchanged);
    }

    #[test]
    fn brightness_next_exits() {
        let mut state = AppState::default();
        state.menu_selection = MenuEntry::Brightness;
        state.in_detail_view = true;

        dispatch(&mut state, Action::ButtonNext);
        assert!(!state.in_detail_view);
    }

    // =========================================================================
    // Rainbow Test
    // =========================================================================

    #[test]
    fn rainbow_select_cycles_string_and_starts() {
        let mut state = AppState::default();
        state.menu_selection = MenuEntry::RainbowTest;
        state.in_detail_view = true;
        assert_eq!(state.rainbow_test.current_string, 0);
        assert_eq!(state.rainbow_test.run_state, RunState::Stopped);

        let effect = dispatch(&mut state, Action::ButtonSelect);
        assert_eq!(state.rainbow_test.current_string, 1);
        assert_eq!(state.rainbow_test.run_state, RunState::Running);
        assert_eq!(effect, AppEffect::RainbowUpdate);
    }

    #[test]
    fn rainbow_next_exits_and_stops() {
        let mut state = AppState::default();
        state.menu_selection = MenuEntry::RainbowTest;
        state.in_detail_view = true;
        state.rainbow_test.run_state = RunState::Running;

        let effect = dispatch(&mut state, Action::ButtonNext);
        assert!(!state.in_detail_view);
        assert_eq!(state.rainbow_test.run_state, RunState::Stopped);
        assert_eq!(effect, AppEffect::RainbowStop);
    }

    #[test]
    fn rainbow_frame_complete_updates_fps() {
        let mut state = AppState::default();
        state.rainbow_test.fps = 0;

        dispatch(&mut state, Action::RainbowFrameComplete { fps: 60 });
        assert_eq!(state.rainbow_test.fps, 60);
    }

    // =========================================================================
    // String Length
    // =========================================================================

    #[test]
    fn string_length_next_increments_pixel() {
        let mut state = AppState::default();
        state.menu_selection = MenuEntry::StringLength;
        state.in_detail_view = true;
        assert_eq!(state.string_length.current_pixel, 1);

        let effect = dispatch(&mut state, Action::ButtonNext);
        assert_eq!(state.string_length.current_pixel, 2);
        assert_eq!(effect, AppEffect::StringLengthUpdate);
    }

    #[test]
    fn string_length_pixel_wraps_at_max() {
        let mut state = AppState::default();
        state.menu_selection = MenuEntry::StringLength;
        state.in_detail_view = true;
        state.string_length.current_pixel = MAX_PIXELS_PER_STRING;

        dispatch(&mut state, Action::ButtonNext);
        assert_eq!(state.string_length.current_pixel, 1);
    }

    #[test]
    fn string_length_select_saves_and_advances() {
        let mut state = AppState::default();
        state.menu_selection = MenuEntry::StringLength;
        state.in_detail_view = true;
        state.string_length.current_string = 0;
        state.string_length.current_pixel = 84;

        let effect = dispatch(&mut state, Action::ButtonSelect);
        assert_eq!(state.string_length.lengths[0], 84);
        assert_eq!(state.string_length.current_string, 1);
        assert_eq!(state.string_length.current_pixel, 1); // Reset for new string
        assert_eq!(effect, AppEffect::StringLengthUpdate);
    }

    #[test]
    fn string_length_exits_after_last_string() {
        let mut state = AppState::default();
        state.menu_selection = MenuEntry::StringLength;
        state.in_detail_view = true;
        state.string_length.current_string = 31;
        state.string_length.current_pixel = 50;

        let effect = dispatch(&mut state, Action::ButtonSelect);
        assert_eq!(state.string_length.lengths[31], 50);
        assert!(!state.in_detail_view);
        assert_eq!(effect, AppEffect::StringLengthStop);
    }

    // =========================================================================
    // Info View
    // =========================================================================

    #[test]
    fn info_next_scrolls() {
        let mut state = AppState::default();
        state.menu_selection = MenuEntry::Info;
        state.in_detail_view = true;
        assert_eq!(state.info_view.scroll_index, 0);

        dispatch(&mut state, Action::ButtonNext);
        assert_eq!(state.info_view.scroll_index, 1);
    }

    #[test]
    fn info_select_on_exit_exits() {
        let mut state = AppState::default();
        state.menu_selection = MenuEntry::Info;
        state.in_detail_view = true;
        state.info_view.scroll_index = NUM_STRINGS as u8; // Exit option

        dispatch(&mut state, Action::ButtonSelect);
        assert!(!state.in_detail_view);
    }

    #[test]
    fn info_select_on_string_is_noop() {
        let mut state = AppState::default();
        state.menu_selection = MenuEntry::Info;
        state.in_detail_view = true;
        state.info_view.scroll_index = 5;

        let effect = dispatch(&mut state, Action::ButtonSelect);
        assert_eq!(effect, AppEffect::Unchanged);
    }

    // =========================================================================
    // Shutdown
    // =========================================================================

    #[test]
    fn shutdown_select_powers_off() {
        let mut state = AppState::default();
        state.menu_selection = MenuEntry::Shutdown;
        state.in_detail_view = true;
        assert!(state.is_powered_on);

        dispatch(&mut state, Action::ButtonSelect);
        assert!(!state.is_powered_on);
        assert!(!state.in_detail_view);
    }

    #[test]
    fn shutdown_next_cancels() {
        let mut state = AppState::default();
        state.menu_selection = MenuEntry::Shutdown;
        state.in_detail_view = true;

        dispatch(&mut state, Action::ButtonNext);
        assert!(state.is_powered_on);
        assert!(!state.in_detail_view);
    }

    // =========================================================================
    // Power State
    // =========================================================================

    #[test]
    fn power_toggle_turns_off() {
        let mut state = AppState::default();
        assert!(state.is_powered_on);

        dispatch(&mut state, Action::PowerToggle);
        assert!(!state.is_powered_on);
    }

    #[test]
    fn powered_off_ignores_most_actions() {
        let mut state = AppState::default();
        state.is_powered_on = false;
        state.brightness_level = 5;

        // Brightness actions should be ignored
        let effect = dispatch(&mut state, Action::BrightnessUp);
        assert_eq!(effect, AppEffect::Unchanged);
        assert_eq!(state.brightness_level, 5); // Unchanged
    }

    #[test]
    fn powered_off_wakes_on_button() {
        let mut state = AppState::default();
        state.is_powered_on = false;

        dispatch(&mut state, Action::ButtonSelect);
        assert!(state.is_powered_on);
    }

    #[test]
    fn powered_off_wakes_on_power_toggle() {
        let mut state = AppState::default();
        state.is_powered_on = false;

        dispatch(&mut state, Action::PowerToggle);
        assert!(state.is_powered_on);
    }

    // =========================================================================
    // Timer
    // =========================================================================

    #[test]
    fn tick_increments_uptime() {
        let mut state = AppState::default();
        assert_eq!(state.uptime_seconds, 0);

        dispatch(&mut state, Action::Tick1s);
        assert_eq!(state.uptime_seconds, 1);

        dispatch(&mut state, Action::Tick1s);
        assert_eq!(state.uptime_seconds, 2);
    }

    // =========================================================================
    // Edge Cases
    // =========================================================================

    #[test]
    fn power_off_stops_rainbow_test() {
        let mut state = AppState::default();
        state.rainbow_test.run_state = RunState::Running;

        dispatch(&mut state, Action::PowerToggle);
        assert!(!state.is_powered_on);
        assert_eq!(state.rainbow_test.run_state, RunState::Stopped);
    }

    #[test]
    fn power_off_stops_string_length_test() {
        let mut state = AppState::default();
        state.menu_selection = MenuEntry::StringLength;
        state.in_detail_view = true;
        state.string_length.run_state = RunState::Running;

        dispatch(&mut state, Action::PowerToggle);
        assert!(!state.is_powered_on);
        assert_eq!(state.string_length.run_state, RunState::Stopped);
    }

    #[test]
    fn rainbow_string_wraps_at_32() {
        let mut state = AppState::default();
        state.menu_selection = MenuEntry::RainbowTest;
        state.in_detail_view = true;
        state.rainbow_test.current_string = 31;

        dispatch(&mut state, Action::ButtonSelect);
        assert_eq!(state.rainbow_test.current_string, 0); // Wraps to 0
    }

    #[test]
    fn info_scroll_wraps_at_end() {
        let mut state = AppState::default();
        state.menu_selection = MenuEntry::Info;
        state.in_detail_view = true;
        state.info_view.scroll_index = NUM_STRINGS as u8; // Exit option (last position)

        dispatch(&mut state, Action::ButtonNext);
        assert_eq!(state.info_view.scroll_index, 0); // Wraps to start
    }

    #[test]
    fn button_on_error_screen_clears_error() {
        let mut state = AppState::default();
        state.boot_state = BootState::Error;

        dispatch(&mut state, Action::ButtonSelect);
        assert_eq!(state.boot_state, BootState::Ready);

        // Same for ButtonNext
        let mut state = AppState::default();
        state.boot_state = BootState::Error;

        dispatch(&mut state, Action::ButtonNext);
        assert_eq!(state.boot_state, BootState::Ready);
    }

    #[test]
    fn uptime_wraps_at_u32_max() {
        let mut state = AppState::default();
        state.uptime_seconds = u32::MAX;

        dispatch(&mut state, Action::Tick1s);
        assert_eq!(state.uptime_seconds, 0); // Wraps
    }

    // =========================================================================
    // Fuzz / Button Mashing Test
    // =========================================================================

    /// Simple LCG pseudo-random for deterministic fuzzing (no rand crate needed)
    struct SimpleRng(u32);

    impl SimpleRng {
        fn next(&mut self) -> u32 {
            self.0 = self.0.wrapping_mul(1103515245).wrapping_add(12345);
            self.0
        }

        fn next_action(&mut self) -> Action {
            match self.next() % 10 {
                0 => Action::ButtonSelect,
                1 => Action::ButtonNext,
                2 => Action::Tick1s,
                3 => Action::PowerToggle,
                4 => Action::BrightnessUp,
                5 => Action::BrightnessDown,
                6 => Action::RainbowFrameComplete { fps: (self.next() % 120) as u16 },
                7 => Action::Boot,
                8 => Action::SdFilesScanned { count: (self.next() % 16) as u8 },
                _ => Action::ButtonSelect,
            }
        }
    }

    /// Verify state invariants that must always hold
    fn check_invariants(state: &AppState) {
        // Brightness must be in valid range
        assert!(state.brightness_level >= BRIGHTNESS_MIN, "brightness below min");
        assert!(state.brightness_level <= BRIGHTNESS_MAX, "brightness above max");

        // Board ID must be in range
        assert!(state.board_id <= 15, "board_id out of range");

        // Menu selection must be valid (enum can't be invalid, but index should work)
        let _ = state.menu_selection.index(); // Should not panic

        // Rainbow string must be in range
        assert!(state.rainbow_test.current_string < 32, "rainbow string out of range");

        // String length test string must be in range
        assert!(state.string_length.current_string < 32, "string length string out of range");

        // String length pixel must be in range
        assert!(state.string_length.current_pixel >= 1, "pixel below 1");
        assert!(state.string_length.current_pixel <= MAX_PIXELS_PER_STRING, "pixel above max");

        // Info scroll must be in range (0..=NUM_STRINGS for exit option)
        assert!(state.info_view.scroll_index <= NUM_STRINGS as u8, "info scroll out of range");
    }

    #[test]
    fn button_mashing_fuzz() {
        let mut rng = SimpleRng(12345); // Deterministic seed
        let mut state = AppState::default();

        // Mash 10,000 random actions
        for i in 0..10_000 {
            let action = rng.next_action();
            dispatch(&mut state, action);

            // Check invariants after every action
            check_invariants(&state);

            // Occasionally verify we can recover from any state
            if i % 1000 == 0 {
                // Power cycle should always work
                if state.is_powered_on {
                    dispatch(&mut state, Action::PowerToggle);
                }
                dispatch(&mut state, Action::PowerToggle);
                assert!(state.is_powered_on, "should be powered on after toggle from off");
            }
        }
    }

    #[test]
    fn rapid_power_cycling() {
        let mut state = AppState::default();

        // Rapidly toggle power 1000 times
        for _ in 0..1000 {
            dispatch(&mut state, Action::PowerToggle);
            check_invariants(&state);
        }

        // Should end up in a valid state
        check_invariants(&state);
    }

    #[test]
    fn rapid_menu_navigation() {
        let mut state = AppState::default();

        // Rapidly navigate menus 1000 times
        for i in 0..1000 {
            let action = if i % 3 == 0 {
                Action::ButtonSelect
            } else {
                Action::ButtonNext
            };
            dispatch(&mut state, action);
            check_invariants(&state);
        }
    }
}
