#include "reducer.h"

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
        case MENU_STRING_TEST:
            new_state.string_test.run_state = TEST_RUNNING;
            break;
        case MENU_TOGGLE_TEST:
            new_state.toggle_test.run_state = TEST_RUNNING;
            break;
        case MENU_RAINBOW_TEST:
            new_state.rainbow_test.run_state = TEST_RUNNING;
            new_state.rainbow_test.frame_count = 0;
            new_state.rainbow_test.fps = 0;
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
        // Exit detail view and stop any running tests
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

// Handle fast tick (for animations)
static AppState handle_tick_fast(const AppState* state) {
    // Only update if rainbow test is running
    if (state->rainbow_test.run_state != TEST_RUNNING) {
        return *state;  // No change
    }

    AppState new_state = app_state_new_version(state);
    new_state.rainbow_test.hue_offset = state->rainbow_test.hue_offset + 1;
    new_state.rainbow_test.frame_count = state->rainbow_test.frame_count + 1;
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

// Handle rainbow frame complete (FPS update)
static AppState handle_rainbow_frame_complete(const AppState* state, const Action* action) {
    if (state->rainbow_test.fps == action->payload.rainbow.fps) {
        return *state;  // No change
    }

    AppState new_state = app_state_new_version(state);
    new_state.rainbow_test.fps = action->payload.rainbow.fps;
    return new_state;
}

// Main reducer function
AppState reduce(const AppState* state, const Action* action) {
    switch (action->type) {
        case ACTION_BUTTON_SELECT:
            return handle_button_select(state);

        case ACTION_BUTTON_NEXT:
            return handle_button_next(state);

        case ACTION_TICK_1S:
            return handle_tick_1s(state);

        case ACTION_TICK_FAST:
            return handle_tick_fast(state);

        case ACTION_BOARD_ADDRESS_UPDATED:
            return handle_board_address_updated(state, action);

        case ACTION_RAINBOW_FRAME_COMPLETE:
            return handle_rainbow_frame_complete(state, action);

        case ACTION_NONE:
        default:
            return *state;  // No change
    }
}
