#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "fseq_player.h"
#include "rainbow_test.h"

// Command types sent from Core 0 to Core 1
typedef enum {
    CORE1_CMD_IDLE = 0,       // Do nothing, wait for next command
    CORE1_CMD_STOP,           // Stop current task, return to idle
    CORE1_CMD_PLAY_FSEQ,      // Start FSEQ playback
    CORE1_CMD_PLAY_RAINBOW,   // Start rainbow test
} core1_cmd_type_t;

// Current task running on Core 1 (readable from Core 0)
typedef enum {
    CORE1_TASK_IDLE = 0,
    CORE1_TASK_FSEQ,
    CORE1_TASK_RAINBOW,
} core1_task_t;

// Initialize Core 1 task system (call once from main, before launching Core 1)
void core1_task_init(fseq_player_t* fseq_ctx, rainbow_test_t* rainbow_ctx);

// Core 1 entry point - runs forever processing commands
void core1_main(void);

// --- Commands (called from Core 0) ---

// Send stop command and wait for Core 1 to become idle
// This is blocking but safe - Core 1 will finish current frame and clean up
void core1_stop_and_wait(void);

// Start FSEQ playback (stops any current task first)
// filename is copied internally - caller's string doesn't need to persist
void core1_start_fseq(const char* filename);

// Start rainbow test (stops any current task first)
void core1_start_rainbow(void);

// --- Status (called from Core 0) ---

// Get current task type
core1_task_t core1_get_current_task(void);

// Check if Core 1 is idle
bool core1_is_idle(void);

// Get FSEQ loop count (for auto-advance detection)
uint32_t core1_get_fseq_loop_count(void);
