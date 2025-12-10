#include "reducer.h"

// Helper: stop all tests and playback
static AppState stop_all_output(AppState state) {
    state.string_test.run_state = TEST_STOPPED;
    state.toggle_test.run_state = TEST_STOPPED;
    state.rainbow_test.run_state = TEST_STOPPED;
    state.sd_card.is_playing = false;
    return state;
}

// Helper: stop all tests except the specified one
static AppState stop_other_tests(AppState state, MenuEntry keep_running) {
    if (keep_running != MENU_STRING_TEST) {
        state.string_test.run_state = TEST_STOPPED;
    }
    if (keep_running != MENU_TOGGLE_TEST) {
        state.toggle_test.run_state = TEST_STOPPED;
    }
    if (keep_running != MENU_RAINBOW_TEST) {
        state.rainbow_test.run_state = TEST_STOPPED;
    }
    return state;
}

// Handle SELECT button in menu view
static AppState handle_select_menu(const AppState* state) {
    AppState new_state = app_state_new_version(state);
    new_state.in_detail_view = true;

    // Stop other tests and start the selected one
    new_state = stop_other_tests(new_state, state->menu_selection);

    switch (state->menu_selection) {
        case MENU_SD_CARD:
            // Trigger a fresh scan when entering SD card view
            new_state.sd_card.needs_scan = true;
            new_state.sd_card.scroll_index = 0;
            break;
        case MENU_STRING_TEST:
            new_state.string_test.run_state = TEST_RUNNING;
            break;
        case MENU_TOGGLE_TEST:
            new_state.toggle_test.run_state = TEST_RUNNING;
            break;
        case MENU_RAINBOW_TEST:
            new_state.rainbow_test.run_state = TEST_RUNNING;
            new_state.rainbow_test.fps = 0;
            break;
        case MENU_SHUTDOWN:
            // Shutdown: power off immediately (don't enter detail view)
            new_state.in_detail_view = false;
            new_state.is_powered_on = false;
            new_state = stop_all_output(new_state);
            break;
        default:
            // INFO and BOARD_ADDRESS just show detail view
            break;
    }

    return new_state;
}

// Handle SELECT button in detail view
static AppState handle_select_detail(const AppState* state) {
    switch (state->menu_selection) {
        case MENU_STRING_TEST:
        case MENU_TOGGLE_TEST: {
            // Toggle stops and exits detail view
            AppState new_state = app_state_new_version(state);
            new_state.in_detail_view = false;
            new_state.string_test.run_state = TEST_STOPPED;
            new_state.toggle_test.run_state = TEST_STOPPED;
            return new_state;
        }

        case MENU_RAINBOW_TEST: {
            // Select advances to next string (doesn't exit)
            AppState new_state = app_state_new_version(state);
            new_state.rainbow_test.current_string =
                (state->rainbow_test.current_string + 1) % 32;
            return new_state;
        }

        case MENU_SD_CARD: {
            // If playing, SELECT stops playback
            if (state->sd_card.is_playing) {
                AppState new_state = app_state_new_version(state);
                new_state.sd_card.is_playing = false;
                return new_state;
            }

            // SELECT on [Main Menu] - exit
            if (state->sd_card.scroll_index >= state->sd_card.file_count) {
                AppState new_state = app_state_new_version(state);
                new_state.in_detail_view = false;
                return new_state;
            }

            // SELECT on a file - start playback
            // Reducer only sets index (pure) - side_effects looks up filename
            AppState new_state = app_state_new_version(state);
            new_state.sd_card.is_playing = true;
            new_state.sd_card.playing_index = state->sd_card.scroll_index;
            return new_state;
        }

        case MENU_BRIGHTNESS: {
            // Cycle brightness 1->2->...->10->1
            AppState new_state = app_state_new_version(state);
            new_state.brightness_level = (state->brightness_level % BRIGHTNESS_MAX) + 1;
            return new_state;
        }

        case MENU_INFO:
        case MENU_BOARD_ADDRESS:
        default: {
            // Exit detail view
            AppState new_state = app_state_new_version(state);
            new_state.in_detail_view = false;
            return new_state;
        }
    }
}

// Handle SELECT button
static AppState handle_button_select(const AppState* state) {
    if (state->in_detail_view) {
        return handle_select_detail(state);
    } else {
        return handle_select_menu(state);
    }
}

// Handle NEXT button
static AppState handle_button_next(const AppState* state) {
    if (state->in_detail_view) {
        // Special handling for SD Card
        if (state->menu_selection == MENU_SD_CARD) {
            // If playing, NEXT stops playback
            if (state->sd_card.is_playing) {
                AppState new_state = app_state_new_version(state);
                new_state.sd_card.is_playing = false;
                return new_state;
            }
            // Scroll through files
            AppState new_state = app_state_new_version(state);
            uint8_t total_items = state->sd_card.file_count + 1;  // +1 for [Main Menu]
            new_state.sd_card.scroll_index = (state->sd_card.scroll_index + 1) % total_items;
            return new_state;
        }

        // Default: Exit detail view and stop any running tests
        AppState new_state = app_state_new_version(state);
        new_state.in_detail_view = false;
        new_state.string_test.run_state = TEST_STOPPED;
        new_state.toggle_test.run_state = TEST_STOPPED;
        new_state.rainbow_test.run_state = TEST_STOPPED;
        return new_state;
    } else {
        // Navigate to next menu item
        AppState new_state = app_state_new_version(state);
        new_state.menu_selection = (state->menu_selection + 1) % MENU_COUNT;
        return new_state;
    }
}

// Handle 1 second tick
static AppState handle_tick_1s(const AppState* state) {
    AppState new_state = app_state_new_version(state);
    new_state.uptime_seconds = state->uptime_seconds + 1;
    return new_state;
}

// Handle board address update
static AppState handle_board_address_updated(const AppState* state, const Action* action) {
    // Only update if values actually changed
    if (state->board_address.adc_value == action->payload.board_address.adc_value &&
        state->board_address.code == action->payload.board_address.code) {
        return *state;  // No change
    }

    AppState new_state = app_state_new_version(state);
    new_state.board_address.adc_value = action->payload.board_address.adc_value;
    new_state.board_address.code = action->payload.board_address.code;
    new_state.board_address.error = action->payload.board_address.error;
    new_state.board_address.margin = action->payload.board_address.margin;
    return new_state;
}

// Handle SD Card Mounted
static AppState handle_sd_mounted(const AppState* state) {
    AppState new_state = app_state_new_version(state);
    new_state.sd_card.mounted = true;
    for(int i=0; i<24; i++) new_state.sd_card.status_msg[i] = 0;
    return new_state;
}

// Handle SD Card Error
static AppState handle_sd_error(const AppState* state, const Action* action) {
    AppState new_state = app_state_new_version(state);
    new_state.sd_card.mounted = false;
    new_state.sd_card.needs_scan = false;  // Scan complete (failed)
    new_state.sd_card.file_count = 0;
    for(int i=0; i<24; i++) {
        new_state.sd_card.status_msg[i] = action->payload.sd_error.message[i];
    }
    return new_state;
}

// Handle SD Files Loaded
static AppState handle_sd_files(const AppState* state, const Action* action) {
    AppState new_state = app_state_new_version(state);
    new_state.sd_card.mounted = true;
    new_state.sd_card.needs_scan = false;  // Scan complete
    new_state.sd_card.file_count = action->payload.sd_files.count;
    new_state.sd_card.scroll_index = 0;
    // Files already stored in sd_file_list static buffer by main.c
    if(new_state.sd_card.file_count == 0) {
        const char* msg = "No .fseq files";
        for(int i=0; msg[i] && i<23; i++) new_state.sd_card.status_msg[i] = msg[i];
        new_state.sd_card.auto_play_pending = false;  // Can't auto-play with no files
    } else if (state->sd_card.auto_play_pending) {
        // Auto-play was requested via IR - start first file
        new_state.sd_card.is_playing = true;
        new_state.sd_card.playing_index = 0;
        new_state.sd_card.auto_play_pending = false;
    }
    return new_state;
}

// Handle rainbow frame complete (FPS update)
static AppState handle_rainbow_frame_complete(const AppState* state, const Action* action) {
    if (state->rainbow_test.fps == action->payload.rainbow.fps) {
        return *state;  // No change
    }

    AppState new_state = app_state_new_version(state);
    new_state.rainbow_test.fps = action->payload.rainbow.fps;
    return new_state;
}

// Handle power toggle (on/off)
static AppState handle_power_toggle(const AppState* state) {
    AppState new_state = app_state_new_version(state);
    new_state.is_powered_on = !state->is_powered_on;

    if (!new_state.is_powered_on) {
        // Powering off: stop all output, reset to main menu
        new_state = stop_all_output(new_state);
        new_state.in_detail_view = false;
        new_state.menu_selection = MENU_INFO;
    }
    // Powering on: just flip the flag, user sees main menu

    return new_state;
}

// Handle skip to next FSEQ file (or start playback if not playing)
static AppState handle_fseq_next(const AppState* state) {
    AppState new_state = app_state_new_version(state);

    // Stop any running tests before starting FSEQ playback
    new_state = stop_all_output(new_state);

    if (state->sd_card.is_playing) {
        // Currently playing: skip to next file
        if (state->sd_card.file_count > 0) {
            new_state.sd_card.is_playing = true;  // Re-enable after stop_all_output
            new_state.sd_card.playing_index = (state->sd_card.playing_index + 1) % state->sd_card.file_count;
        }
    } else {
        // Not playing: trigger SD scan and start playback
        // If files already loaded, start first file
        if (state->sd_card.file_count > 0) {
            new_state.sd_card.is_playing = true;
            new_state.sd_card.playing_index = 0;
            // Update menu state to show SD card view
            new_state.menu_selection = MENU_SD_CARD;
            new_state.in_detail_view = true;
            new_state.sd_card.scroll_index = 0;
        } else {
            // Need to scan first - trigger scan by entering SD view
            new_state.menu_selection = MENU_SD_CARD;
            new_state.in_detail_view = true;
            new_state.sd_card.needs_scan = true;
            // Set flag to auto-play after scan completes
            new_state.sd_card.auto_play_pending = true;
        }
    }

    return new_state;
}

// Handle brightness up
static AppState handle_brightness_up(const AppState* state) {
    if (state->brightness_level >= BRIGHTNESS_MAX) {
        return *state;  // Already at max
    }
    AppState new_state = app_state_new_version(state);
    new_state.brightness_level = state->brightness_level + 1;
    return new_state;
}

// Handle brightness down
static AppState handle_brightness_down(const AppState* state) {
    if (state->brightness_level <= BRIGHTNESS_MIN) {
        return *state;  // Already at min
    }
    AppState new_state = app_state_new_version(state);
    new_state.brightness_level = state->brightness_level - 1;
    return new_state;
}

// Main reducer function
AppState reduce(const AppState* state, const Action* action) {
    // If powered off, only respond to buttons (wake up) or power toggle
    if (!state->is_powered_on) {
        if (action->type == ACTION_BUTTON_SELECT ||
            action->type == ACTION_BUTTON_NEXT ||
            action->type == ACTION_POWER_TOGGLE) {
            // Any of these wakes up
            AppState new_state = app_state_new_version(state);
            new_state.is_powered_on = true;
            return new_state;
        }
        // Ignore other actions when powered off
        return *state;
    }

    switch (action->type) {
        case ACTION_BUTTON_SELECT:
            return handle_button_select(state);

        case ACTION_BUTTON_NEXT:
            return handle_button_next(state);

        case ACTION_POWER_TOGGLE:
            return handle_power_toggle(state);

        case ACTION_TICK_1S:
            return handle_tick_1s(state);

        case ACTION_BOARD_ADDRESS_UPDATED:
            return handle_board_address_updated(state, action);

        case ACTION_SD_CARD_MOUNTED:
            return handle_sd_mounted(state);

        case ACTION_SD_CARD_ERROR:
            return handle_sd_error(state, action);

        case ACTION_SD_FILES_LOADED:
            return handle_sd_files(state, action);

        case ACTION_RAINBOW_FRAME_COMPLETE:
            return handle_rainbow_frame_complete(state, action);

        case ACTION_FSEQ_NEXT:
            return handle_fseq_next(state);

        case ACTION_BRIGHTNESS_UP:
            return handle_brightness_up(state);

        case ACTION_BRIGHTNESS_DOWN:
            return handle_brightness_down(state);

        case ACTION_NONE:
        default:
            return *state;  // No change
    }
}
