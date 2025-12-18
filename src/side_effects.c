#include "side_effects.h"
#include "core1_task.h"
#include "views.h"
#include "flash_settings.h"
#include "pico/stdlib.h"
#include "pb_led_driver.h"

// Check if test run state changed
static bool string_test_changed(const AppState* old, const AppState* new) {
    return old->string_test.run_state != new->string_test.run_state;
}

static bool toggle_test_changed(const AppState* old, const AppState* new) {
    return old->toggle_test.run_state != new->toggle_test.run_state;
}

static bool rainbow_test_changed(const AppState* old, const AppState* new) {
    return old->rainbow_test.run_state != new->rainbow_test.run_state;
}

static bool rainbow_string_changed(const AppState* old, const AppState* new) {
    return old->rainbow_test.current_string != new->rainbow_test.current_string;
}

static bool string_length_test_changed(const AppState* old, const AppState* new) {
    return old->string_length.run_state != new->string_length.run_state;
}

static bool string_length_position_changed(const AppState* old, const AppState* new) {
    return old->string_length.current_string != new->string_length.current_string ||
           old->string_length.current_pixel != new->string_length.current_pixel;
}

static bool fseq_playback_changed(const AppState* old, const AppState* new) {
    return old->sd_card.is_playing != new->sd_card.is_playing;
}

static bool fseq_file_changed(const AppState* old, const AppState* new) {
    // Detect skip to next file while playing
    return new->sd_card.is_playing &&
           old->sd_card.is_playing &&
           old->sd_card.playing_index != new->sd_card.playing_index;
}

static bool power_state_changed(const AppState* old, const AppState* new) {
    return old->is_powered_on != new->is_powered_on;
}

static bool brightness_changed(const AppState* old, const AppState* new) {
    return old->brightness_level != new->brightness_level;
}

// Convert brightness level (1-10) to hardware value (0-255)
static uint8_t brightness_level_to_hw(uint8_t level) {
    // Map 1-10 to roughly equal perceptual steps
    // Level 1 = 25, Level 10 = 255
    if (level < 1) level = 1;
    if (level > 10) level = 10;
    return (uint8_t)(level * 25 + (level > 1 ? 5 : 0));
}

void side_effects_init(HardwareContext* hw) {
    // Hardware contexts are initialized in main
    (void)hw;
}

void side_effects_apply(const HardwareContext* hw,
                        const AppState* old_state,
                        const AppState* new_state) {
    // Handle power state changes
    if (power_state_changed(old_state, new_state)) {
        if (!new_state->is_powered_on) {
            // Powering off: stop Core 1 task and all tests
            core1_stop_and_wait();
            string_test_stop(hw->string_test);
            toggle_test_stop(hw->toggle_test);
            string_length_test_stop(hw->string_length_test);
            // View will show blank display
        }
        // Powering on: just render, state already reset to menu
    }

    // Skip all other side effects if powered off
    if (!new_state->is_powered_on) {
        views_render(hw->display, new_state);
        return;
    }

    // Handle brightness changes
    if (brightness_changed(old_state, new_state)) {
        uint8_t hw_brightness = brightness_level_to_hw(new_state->brightness_level);
        pb_set_global_brightness(hw_brightness);
    }

    // Handle string test state changes
    if (string_test_changed(old_state, new_state)) {
        if (new_state->string_test.run_state == TEST_RUNNING) {
            string_test_start(hw->string_test);
        } else {
            string_test_stop(hw->string_test);
        }
    }

    // Handle toggle test state changes
    if (toggle_test_changed(old_state, new_state)) {
        if (new_state->toggle_test.run_state == TEST_RUNNING) {
            toggle_test_start(hw->toggle_test);
        } else {
            toggle_test_stop(hw->toggle_test);
        }
    }

    // Handle rainbow test state changes - runs on Core 1
    if (rainbow_test_changed(old_state, new_state)) {
        if (new_state->rainbow_test.run_state == TEST_RUNNING) {
            core1_start_rainbow();
        } else {
            core1_stop_and_wait();
        }
    }

    // Handle rainbow string changes (while running)
    if (new_state->rainbow_test.run_state == TEST_RUNNING &&
        rainbow_string_changed(old_state, new_state)) {
        rainbow_test_next_string(hw->rainbow_test);
    }

    // Handle string length test state changes
    if (string_length_test_changed(old_state, new_state)) {
        if (new_state->string_length.run_state == TEST_RUNNING) {
            string_length_test_start(hw->string_length_test);
        } else {
            string_length_test_stop(hw->string_length_test);
        }
    }

    // Handle string length position changes (while running)
    if (new_state->string_length.run_state == TEST_RUNNING &&
        string_length_position_changed(old_state, new_state)) {
        string_length_test_update(hw->string_length_test,
                                  new_state->string_length.current_string,
                                  new_state->string_length.current_pixel);
    }

    // Handle FSEQ playback state changes - runs on Core 1
    if (fseq_playback_changed(old_state, new_state)) {
        if (new_state->sd_card.is_playing) {
            // Start playback - look up filename from static buffer
            const char* filename = sd_file_list[new_state->sd_card.playing_index];
            core1_start_fseq(filename);
        } else {
            core1_stop_and_wait();
        }
    }

    // Handle skip to next file during playback
    if (fseq_file_changed(old_state, new_state)) {
        // Start new file (core1_start_fseq stops current task first)
        const char* filename = sd_file_list[new_state->sd_card.playing_index];
        core1_start_fseq(filename);
    }

    // Always render view on state change
    views_render(hw->display, new_state);

    // Check if settings need saving (debounced)
    flash_settings_check_save(new_state->brightness_level,
                              new_state->sd_card.is_playing,
                              new_state->sd_card.playing_index,
                              new_state->sd_card.auto_loop);
}

bool side_effects_tick(const HardwareContext* hw, const AppState* state) {
    // Skip all tasks if powered off
    if (!state->is_powered_on) {
        return false;
    }

    // Run string test task if active
    if (state->string_test.run_state == TEST_RUNNING) {
        string_test_task(hw->string_test);
    }

    // Run toggle test task if active
    if (state->toggle_test.run_state == TEST_RUNNING) {
        toggle_test_task(hw->toggle_test);
    }

    // Rainbow test and FSEQ run on Core 1, no tick needed here
    return state->rainbow_test.run_state == TEST_RUNNING;
}

uint16_t side_effects_get_rainbow_fps(const HardwareContext* hw) {
    return rainbow_test_get_fps(hw->rainbow_test);
}
