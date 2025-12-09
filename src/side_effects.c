#include "side_effects.h"
#include "views.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"

#include <stdio.h>

// Core1 control (defined in main.c)
extern volatile bool rainbow_core1_running;
extern void core1_rainbow_entry(void);

// FSEQ playback Core1 control
extern volatile bool fseq_core1_running;
extern void core1_fseq_entry(void);

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

static bool fseq_playback_changed(const AppState* old, const AppState* new) {
    return old->sd_card.is_playing != new->sd_card.is_playing;
}

void side_effects_init(HardwareContext* hw) {
    // Hardware contexts are initialized in main
    (void)hw;
}

void side_effects_apply(const HardwareContext* hw,
                        const AppState* old_state,
                        const AppState* new_state) {
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

    // Handle rainbow test state changes - runs on core1
    if (rainbow_test_changed(old_state, new_state)) {
        if (new_state->rainbow_test.run_state == TEST_RUNNING) {
            // Guard: only launch if Core1 is not already running
            if (!rainbow_core1_running && !fseq_core1_running) {
                rainbow_test_start(hw->rainbow_test);
                rainbow_core1_running = true;
                multicore_launch_core1(core1_rainbow_entry);
            }
        } else {
            // Signal Core 1 to stop and wait for it to exit gracefully
            rainbow_core1_running = false;
            sleep_ms(20);  // Allow Core 1 to finish current frame and exit loop
            multicore_reset_core1();
            rainbow_test_stop(hw->rainbow_test);
        }
    }

    // Handle rainbow string changes (while running)
    if (new_state->rainbow_test.run_state == TEST_RUNNING &&
        rainbow_string_changed(old_state, new_state)) {
        rainbow_test_next_string(hw->rainbow_test);
    }

    // Handle FSEQ playback state changes - runs on core1
    if (fseq_playback_changed(old_state, new_state)) {
        if (new_state->sd_card.is_playing) {
            // Guard: only launch if Core1 is not already running
            if (!fseq_core1_running && !rainbow_core1_running) {
                // Start playback - look up filename from static buffer
                const char* filename = sd_file_list[new_state->sd_card.playing_index];
                fseq_player_start(hw->fseq_player, filename);
                fseq_core1_running = true;
                multicore_launch_core1(core1_fseq_entry);
            }
        } else {
            // Signal Core 1 to stop - let it exit gracefully to close file properly
            fseq_core1_running = false;
            // Wait for Core 1 to exit (it checks the flag each frame)
            // At 60fps, frames are ~16ms, so 100ms is plenty
            sleep_ms(100);
            // Now safe to reset (Core 1 should have exited and closed file)
            multicore_reset_core1();
            fseq_player_stop(hw->fseq_player);
        }
    }

    // Always render view on state change
    views_render(hw->display, new_state);
}

bool side_effects_tick(const HardwareContext* hw, const AppState* state) {
    // Run string test task if active
    if (state->string_test.run_state == TEST_RUNNING) {
        string_test_task(hw->string_test);
    }

    // Run toggle test task if active
    if (state->toggle_test.run_state == TEST_RUNNING) {
        toggle_test_task(hw->toggle_test);
    }

    // Rainbow test runs on core1, no tick needed here
    return state->rainbow_test.run_state == TEST_RUNNING;
}

uint16_t side_effects_get_rainbow_fps(const HardwareContext* hw) {
    return rainbow_test_get_fps(hw->rainbow_test);
}
