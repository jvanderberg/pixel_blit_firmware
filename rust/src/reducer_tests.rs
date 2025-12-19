//! Tests for the reducer - run with `cargo test` on host.

#[cfg(test)]
mod tests {
    use crate::action::Action;
    use crate::reducer::AppReducer;
    use crate::state::*;
    use reducto::{Reducer, Outcome};

    fn dispatch(state: AppState, action: Action) -> AppState {
        match AppReducer::reduce(state, action) {
            Outcome::Changed(s) | Outcome::Unchanged(s) => s,
        }
    }

    // =========================================================================
    // Boot
    // =========================================================================

    #[test]
    fn boot_triggers_render() {
        let state = AppState::default();
        let result = AppReducer::reduce(state, Action::Boot);
        assert!(matches!(result, Outcome::Changed(_)));
    }

    // =========================================================================
    // Menu Navigation
    // =========================================================================

    #[test]
    fn next_cycles_menu_items() {
        let mut state = AppState::default();
        assert_eq!(state.menu_selection, MenuEntry::Info);

        state = dispatch(state, Action::ButtonNext);
        assert_eq!(state.menu_selection, MenuEntry::BoardAddress);

        state = dispatch(state, Action::ButtonNext);
        assert_eq!(state.menu_selection, MenuEntry::SdCard);

        state = dispatch(state, Action::ButtonNext);
        assert_eq!(state.menu_selection, MenuEntry::RainbowTest);

        state = dispatch(state, Action::ButtonNext);
        assert_eq!(state.menu_selection, MenuEntry::StringLength);

        state = dispatch(state, Action::ButtonNext);
        assert_eq!(state.menu_selection, MenuEntry::Brightness);

        state = dispatch(state, Action::ButtonNext);
        assert_eq!(state.menu_selection, MenuEntry::Shutdown);

        // Wraps back to Info
        state = dispatch(state, Action::ButtonNext);
        assert_eq!(state.menu_selection, MenuEntry::Info);
    }

    #[test]
    fn select_enters_detail_view() {
        let mut state = AppState::default();
        assert!(!state.in_detail_view);

        state = dispatch(state, Action::ButtonSelect);
        assert!(state.in_detail_view);
    }

    #[test]
    fn next_in_main_menu_does_not_enter_detail() {
        let mut state = AppState::default();
        state = dispatch(state, Action::ButtonNext);
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

        state = dispatch(state, Action::ButtonSelect);
        assert_eq!(state.board_id, 1);

        // Cycle to max
        for _ in 0..14 {
            state = dispatch(state, Action::ButtonSelect);
        }
        assert_eq!(state.board_id, 15);

        // Wraps to 0
        state = dispatch(state, Action::ButtonSelect);
        assert_eq!(state.board_id, 0);
    }

    #[test]
    fn board_address_next_exits() {
        let mut state = AppState::default();
        state.menu_selection = MenuEntry::BoardAddress;
        state.in_detail_view = true;

        state = dispatch(state, Action::ButtonNext);
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

        state = dispatch(state, Action::ButtonSelect);
        assert_eq!(state.brightness_level, 1); // Wraps to min

        state = dispatch(state, Action::ButtonSelect);
        assert_eq!(state.brightness_level, 2);
    }

    #[test]
    fn brightness_up_down_from_ir() {
        let mut state = AppState::default();
        state.brightness_level = 5;

        state = dispatch(state, Action::BrightnessUp);
        assert_eq!(state.brightness_level, 6);

        state = dispatch(state, Action::BrightnessDown);
        assert_eq!(state.brightness_level, 5);
    }

    #[test]
    fn brightness_clamps_at_bounds() {
        let mut state = AppState::default();
        state.brightness_level = BRIGHTNESS_MAX;

        state = dispatch(state, Action::BrightnessUp);
        assert_eq!(state.brightness_level, BRIGHTNESS_MAX);

        state.brightness_level = BRIGHTNESS_MIN;
        state = dispatch(state, Action::BrightnessDown);
        assert_eq!(state.brightness_level, BRIGHTNESS_MIN);
    }

    #[test]
    fn brightness_next_exits() {
        let mut state = AppState::default();
        state.menu_selection = MenuEntry::Brightness;
        state.in_detail_view = true;

        state = dispatch(state, Action::ButtonNext);
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

        state = dispatch(state, Action::ButtonSelect);
        assert_eq!(state.rainbow_test.current_string, 1);
        assert_eq!(state.rainbow_test.run_state, RunState::Running);
    }

    #[test]
    fn rainbow_next_exits_and_stops() {
        let mut state = AppState::default();
        state.menu_selection = MenuEntry::RainbowTest;
        state.in_detail_view = true;
        state.rainbow_test.run_state = RunState::Running;

        state = dispatch(state, Action::ButtonNext);
        assert!(!state.in_detail_view);
        assert_eq!(state.rainbow_test.run_state, RunState::Stopped);
    }

    #[test]
    fn rainbow_frame_complete_updates_fps() {
        let mut state = AppState::default();
        state.rainbow_test.fps = 0;

        state = dispatch(state, Action::RainbowFrameComplete { fps: 60 });
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

        state = dispatch(state, Action::ButtonNext);
        assert_eq!(state.string_length.current_pixel, 2);
    }

    #[test]
    fn string_length_pixel_wraps_at_max() {
        let mut state = AppState::default();
        state.menu_selection = MenuEntry::StringLength;
        state.in_detail_view = true;
        state.string_length.current_pixel = MAX_PIXELS_PER_STRING;

        state = dispatch(state, Action::ButtonNext);
        assert_eq!(state.string_length.current_pixel, 1);
    }

    #[test]
    fn string_length_select_saves_and_advances() {
        let mut state = AppState::default();
        state.menu_selection = MenuEntry::StringLength;
        state.in_detail_view = true;
        state.string_length.current_string = 0;
        state.string_length.current_pixel = 84;

        state = dispatch(state, Action::ButtonSelect);
        assert_eq!(state.string_length.lengths[0], 84);
        assert_eq!(state.string_length.current_string, 1);
        assert_eq!(state.string_length.current_pixel, 1); // Reset for new string
    }

    #[test]
    fn string_length_exits_after_last_string() {
        let mut state = AppState::default();
        state.menu_selection = MenuEntry::StringLength;
        state.in_detail_view = true;
        state.string_length.current_string = 31;
        state.string_length.current_pixel = 50;

        state = dispatch(state, Action::ButtonSelect);
        assert_eq!(state.string_length.lengths[31], 50);
        assert!(!state.in_detail_view);
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

        state = dispatch(state, Action::ButtonNext);
        assert_eq!(state.info_view.scroll_index, 1);
    }

    #[test]
    fn info_select_on_exit_exits() {
        let mut state = AppState::default();
        state.menu_selection = MenuEntry::Info;
        state.in_detail_view = true;
        state.info_view.scroll_index = NUM_STRINGS as u8; // Exit option

        state = dispatch(state, Action::ButtonSelect);
        assert!(!state.in_detail_view);
    }

    #[test]
    fn info_select_on_string_is_noop() {
        let mut state = AppState::default();
        state.menu_selection = MenuEntry::Info;
        state.in_detail_view = true;
        state.info_view.scroll_index = 5;

        let result = AppReducer::reduce(state, Action::ButtonSelect);
        assert!(matches!(result, Outcome::Unchanged(_)));
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

        state = dispatch(state, Action::ButtonSelect);
        assert!(!state.is_powered_on);
        assert!(!state.in_detail_view);
    }

    #[test]
    fn shutdown_next_cancels() {
        let mut state = AppState::default();
        state.menu_selection = MenuEntry::Shutdown;
        state.in_detail_view = true;

        state = dispatch(state, Action::ButtonNext);
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

        state = dispatch(state, Action::PowerToggle);
        assert!(!state.is_powered_on);
    }

    #[test]
    fn powered_off_ignores_most_actions() {
        let mut state = AppState::default();
        state.is_powered_on = false;
        state.brightness_level = 5;

        // Brightness actions should be ignored
        let result = AppReducer::reduce(state.clone(), Action::BrightnessUp);
        assert!(matches!(result, Outcome::Unchanged(_)));
    }

    #[test]
    fn powered_off_wakes_on_button() {
        let mut state = AppState::default();
        state.is_powered_on = false;

        state = dispatch(state, Action::ButtonSelect);
        assert!(state.is_powered_on);
    }

    #[test]
    fn powered_off_wakes_on_power_toggle() {
        let mut state = AppState::default();
        state.is_powered_on = false;

        state = dispatch(state, Action::PowerToggle);
        assert!(state.is_powered_on);
    }

    // =========================================================================
    // Timer
    // =========================================================================

    #[test]
    fn tick_increments_uptime() {
        let mut state = AppState::default();
        assert_eq!(state.uptime_seconds, 0);

        state = dispatch(state, Action::Tick1s);
        assert_eq!(state.uptime_seconds, 1);

        state = dispatch(state, Action::Tick1s);
        assert_eq!(state.uptime_seconds, 2);
    }
}
