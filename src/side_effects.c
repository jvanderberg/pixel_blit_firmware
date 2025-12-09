#include "side_effects.h"
#include "views.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/sync.h"  // For __dmb() memory barrier

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

static bool fseq_file_changed(const AppState* old, const AppState* new) {
    // Detect skip to next file while playing
    return new->sd_card.is_playing &&
           old->sd_card.is_playing &&
           old->sd_card.playing_index != new->sd_card.playing_index;
}

static bool power_state_changed(const AppState* old, const AppState* new) {
    return old->is_powered_on != new->is_powered_on;
}

// Stop all running Core1 tasks and tests
static void stop_all_core1_tasks(const HardwareContext* hw) {
    // Stop rainbow test if running on Core1
    if (rainbow_core1_running) {
        rainbow_core1_running = false;
        __dmb();
        sleep_ms(20);
        multicore_reset_core1();
        rainbow_test_stop(hw->rainbow_test);
    }

    // Stop FSEQ playback if running on Core1
    if (fseq_core1_running) {
        printf("POWER: Stopping FSEQ - setting flags\n");
        // Set both flags so Core1 can exit at multiple check points
        fseq_core1_running = false;
        hw->fseq_player->stop_requested = true;
        __dmb();
        printf("POWER: Waiting 100ms for Core1\n");
        sleep_ms(100);
        printf("POWER: Resetting Core1\n");
        multicore_reset_core1();
        printf("POWER: Calling fseq_player_stop\n");
        fseq_player_stop(hw->fseq_player);
        printf("POWER: FSEQ stopped\n");
    }
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
        printf("POWER: State changed -> %s\n", new_state->is_powered_on ? "ON" : "OFF");
        if (!new_state->is_powered_on) {
            // Powering off: stop all output
            printf("POWER: Stopping all Core1 tasks\n");
            stop_all_core1_tasks(hw);
            string_test_stop(hw->string_test);
            toggle_test_stop(hw->toggle_test);
            // View will show blank display
        }
        // Powering on: just render, state already reset to menu
    }

    // Skip all other side effects if powered off
    if (!new_state->is_powered_on) {
        views_render(hw->display, new_state);
        return;
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

    // Handle rainbow test state changes - runs on core1
    if (rainbow_test_changed(old_state, new_state)) {
        if (new_state->rainbow_test.run_state == TEST_RUNNING) {
            // Guard: only launch if Core1 is not already running
            if (!rainbow_core1_running && !fseq_core1_running) {
                rainbow_test_start(hw->rainbow_test);
                rainbow_core1_running = true;
                __dmb();  // Ensure flag is visible to Core1 before launch
                multicore_launch_core1(core1_rainbow_entry);
            }
        } else {
            // Signal Core 1 to stop and wait for it to exit gracefully
            rainbow_core1_running = false;
            __dmb();  // Ensure flag is visible to Core1
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
                __dmb();  // Ensure flag is visible to Core1 before launch
                multicore_launch_core1(core1_fseq_entry);
            }
        } else {
            // Signal Core 1 to stop - let it exit gracefully to close file properly
            fseq_core1_running = false;
            __dmb();  // Ensure flag is visible to Core1
            // Wait for Core 1 to exit (it checks the flag each frame)
            // At 60fps, frames are ~16ms, so 100ms is plenty
            sleep_ms(100);
            // Now safe to reset (Core 1 should have exited and closed file)
            multicore_reset_core1();
            fseq_player_stop(hw->fseq_player);
        }
    }

    // Handle skip to next file during playback
    if (fseq_file_changed(old_state, new_state)) {
        // Stop current playback
        fseq_core1_running = false;
        __dmb();
        sleep_ms(100);
        multicore_reset_core1();
        fseq_player_stop(hw->fseq_player);

        // Start new file
        const char* filename = sd_file_list[new_state->sd_card.playing_index];
        fseq_player_start(hw->fseq_player, filename);
        fseq_core1_running = true;
        __dmb();
        multicore_launch_core1(core1_fseq_entry);
    }

    // Always render view on state change
    views_render(hw->display, new_state);
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

    // Rainbow test runs on core1, no tick needed here
    return state->rainbow_test.run_state == TEST_RUNNING;
}

uint16_t side_effects_get_rainbow_fps(const HardwareContext* hw) {
    return rainbow_test_get_fps(hw->rainbow_test);
}
