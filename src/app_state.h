#pragma once

#include <stdint.h>
#include <stdbool.h>

// Menu entries
typedef enum {
    MENU_INFO = 0,
    MENU_BOARD_ADDRESS,
    MENU_STRING_TEST,
    MENU_TOGGLE_TEST,
    MENU_RAINBOW_TEST,
    MENU_COUNT
} MenuEntry;

// Test run states
typedef enum {
    TEST_STOPPED = 0,
    TEST_RUNNING,
} TestRunState;

// Board address decode result
typedef struct {
    uint16_t adc_value;
    uint8_t code;
    uint16_t error;
    uint16_t margin;
} BoardAddressInfo;

// String test state
typedef struct {
    TestRunState run_state;
} StringTestState;

// Toggle test state
typedef struct {
    TestRunState run_state;
} ToggleTestState;

// Rainbow test state
typedef struct {
    TestRunState run_state;
    uint8_t current_string;
    uint8_t hue_offset;
    uint32_t frame_count;
    uint16_t fps;
} RainbowTestState;

// Complete application state
typedef struct {
    uint32_t version;  // Incremented on every state change

    // UI State
    MenuEntry menu_selection;
    bool in_detail_view;

    // Board info
    BoardAddressInfo board_address;

    // Test states
    StringTestState string_test;
    ToggleTestState toggle_test;
    RainbowTestState rainbow_test;

    // System
    uint32_t uptime_seconds;
} AppState;

// Initialize default state
static inline AppState app_state_init(void) {
    AppState state = {
        .version = 0,
        .menu_selection = MENU_INFO,
        .in_detail_view = false,
        .board_address = {0},
        .string_test = {.run_state = TEST_STOPPED},
        .toggle_test = {.run_state = TEST_STOPPED},
        .rainbow_test = {
            .run_state = TEST_STOPPED,
            .current_string = 0,
            .hue_offset = 0,
            .frame_count = 0,
            .fps = 0,
        },
        .uptime_seconds = 0,
    };
    return state;
}

// Check if state changed (O(1) version comparison)
static inline bool app_state_dirty(const AppState* old_state, const AppState* new_state) {
    return old_state->version != new_state->version;
}

// Create a new state with incremented version (for use in reducer)
static inline AppState app_state_new_version(const AppState* state) {
    AppState new_state = *state;
    new_state.version++;
    return new_state;
}
