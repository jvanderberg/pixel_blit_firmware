#pragma once

#include <stdint.h>
#include <stdbool.h>

// Menu entries
typedef enum {
    MENU_INFO = 0,
    MENU_BOARD_ADDRESS,
    MENU_SD_CARD,
    MENU_STRING_TEST,
    MENU_TOGGLE_TEST,
    MENU_RAINBOW_TEST,
    MENU_STRING_LENGTH,
    MENU_BRIGHTNESS,
    MENU_SHUTDOWN,
    MENU_COUNT
} MenuEntry;

// Test run states
typedef enum {
    TEST_STOPPED = 0,
    TEST_RUNNING,
} TestRunState;

// SD Card State
#define SD_MAX_FILES 16
#define SD_FILENAME_LEN 32  // Support long filenames

typedef struct {
    bool mounted;
    bool needs_scan;               // True when entering view, false after scan
    char status_msg[24];           // Status message (e.g., "Mount Failed")
    uint8_t file_count;            // Number of .fseq files found
    uint8_t scroll_index;          // Current scroll position
    // NOTE: File list stored in static buffer (sd_file_list), not here

    // Playback state
    bool is_playing;
    uint8_t playing_index;         // Index into sd_file_list (reducer stays pure)
    bool auto_play_pending;        // Start playback after SD scan completes
} SdCardState;

// External static file list (defined in main.c)
extern char sd_file_list[SD_MAX_FILES][SD_FILENAME_LEN];

// Board address decode result
typedef struct {
    uint16_t adc_value;
    uint8_t code;
    uint16_t error;
    uint16_t margin;
} BoardAddressInfo;

// Info view state
typedef struct {
    uint8_t scroll_index;          // Current scroll position in string config list
} InfoViewState;

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
    uint16_t fps;
} RainbowTestState;

// String length tool state
#define STRING_LENGTH_MAX_PIXELS 512  // Must match PB_MAX_PIXELS
#define STRING_LENGTH_NUM_STRINGS 32

typedef struct {
    TestRunState run_state;
    uint8_t current_string;        // 0-31
    uint16_t current_pixel;        // Current pixel position being tested
    uint16_t lengths[STRING_LENGTH_NUM_STRINGS];  // Recorded lengths
} StringLengthState;

// Brightness levels (1-10 map to hardware values)
#define BRIGHTNESS_MIN 1
#define BRIGHTNESS_MAX 10
#define BRIGHTNESS_DEFAULT 10

// Complete application state
typedef struct {
    uint32_t version;  // Incremented on every state change

    // Power state
    bool is_powered_on;  // When false, display off, all output stopped

    // Global brightness (1-10)
    uint8_t brightness_level;

    // UI State
    MenuEntry menu_selection;
    bool in_detail_view;

    // Board info
    BoardAddressInfo board_address;

    // Info view
    InfoViewState info_view;

    // SD Card
    SdCardState sd_card;

    // Test states
    StringTestState string_test;
    ToggleTestState toggle_test;
    RainbowTestState rainbow_test;
    StringLengthState string_length;

    // System
    uint32_t uptime_seconds;
} AppState;

// Initialize default state
static inline AppState app_state_init(void) {
    AppState state = {
        .version = 0,
        .is_powered_on = true,
        .brightness_level = BRIGHTNESS_DEFAULT,
        .menu_selection = MENU_INFO,
        .in_detail_view = false,
        .board_address = {0},
        .info_view = {.scroll_index = 0},
        .sd_card = {
            .mounted = false,
            .needs_scan = false,
            .status_msg = "Not scanned",
            .file_count = 0,
            .scroll_index = 0,
            .is_playing = false,
            .playing_index = 0,
        },
        .string_test = {.run_state = TEST_STOPPED},
        .toggle_test = {.run_state = TEST_STOPPED},
        .rainbow_test = {
            .run_state = TEST_STOPPED,
            .current_string = 0,
            .fps = 0,
        },
        .string_length = {
            .run_state = TEST_STOPPED,
            .current_string = 0,
            .current_pixel = 0,
            .lengths = {0},
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
