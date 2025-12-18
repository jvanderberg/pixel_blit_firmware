#include "core1_task.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/flash.h"
#include "hardware/sync.h"
#include "fseq_player.h"
#include "rainbow_test.h"
#include <string.h>
#include <stdio.h>

// Shared state between cores (written by Core 0, read by Core 1)
static volatile core1_task_t current_task = CORE1_TASK_IDLE;
static volatile core1_task_t previous_task = CORE1_TASK_IDLE;  // Track for driver reuse
static volatile bool stop_requested = false;
static volatile uint32_t fseq_loop_count = 0;

// Command passing via shared memory (FIFO is used by flash_safe_execute)
static volatile core1_cmd_type_t pending_cmd = CORE1_CMD_IDLE;
static volatile bool cmd_pending = false;

// Context pointers (set during init)
static fseq_player_t* g_fseq_ctx = NULL;
static rainbow_test_t* g_rainbow_ctx = NULL;

// Filename buffer for FSEQ playback (written by Core 0 before sending command)
static char g_filename[32];

// --- Internal: Check if stop requested (non-blocking) ---
static bool check_stop_requested(void) {
    __dmb();
    if (stop_requested) {
        return true;
    }

    // Also check for new pending commands
    // If there's a command waiting, we should stop current task
    if (cmd_pending) {
        return true;
    }

    return false;
}

// --- Internal: FSEQ playback task ---
static bool run_fseq_task(void) {
    fseq_player_t* ctx = g_fseq_ctx;
    if (!ctx) return false;

    // Start playback (opens file, creates driver, parses header)
    if (!fseq_player_start(ctx, g_filename)) {
        printf("Core1: Failed to start FSEQ\n");
        return false;
    }

    printf("Core1: FSEQ task starting: %s\n", g_filename);

    // Run the playback loop until stop is requested
    fseq_player_run_loop(ctx, check_stop_requested);

    // Clean up (close file, destroy driver)
    fseq_player_cleanup(ctx);

    printf("Core1: FSEQ task ended\n");
    return true;
}

// --- Internal: Rainbow test loop (runs until stop) ---
static bool run_rainbow_task(void) {
    rainbow_test_t* ctx = g_rainbow_ctx;
    if (!ctx) return false;

    printf("Core1: Rainbow task starting\n");

    // Start the test (creates driver)
    rainbow_test_start(ctx);

    // Run frames until stopped
    while (!check_stop_requested()) {
        rainbow_test_task(ctx);
    }

    // Clean up
    rainbow_test_stop(ctx);

    printf("Core1: Rainbow task ended\n");
    return true;
}

// --- Core 1 main loop ---
void core1_main(void) {
    // Allow Core 0 to pause us for flash operations
    flash_safe_execute_core_init();

    // Ensure clean state before processing commands
    // (static initializers may not be visible to Core 1 immediately)
    pending_cmd = CORE1_CMD_IDLE;
    cmd_pending = false;
    stop_requested = false;
    previous_task = CORE1_TASK_IDLE;
    current_task = CORE1_TASK_IDLE;
    __dmb();

    printf("Core1: Started, waiting for commands\n");

    while (true) {
        // Poll for command from Core 0 via shared memory
        __dmb();
        if (!cmd_pending) {
            tight_loop_contents();
            continue;
        }

        // Read and clear the pending command
        core1_cmd_type_t cmd = pending_cmd;
        cmd_pending = false;
        stop_requested = false;
        __dmb();

        // Handle driver cleanup when switching between task types
        // FSEQ keeps driver alive between files, so we need to shutdown when leaving FSEQ
        if (previous_task == CORE1_TASK_FSEQ && cmd != CORE1_CMD_PLAY_FSEQ) {
            fseq_player_shutdown(g_fseq_ctx);
        }

        switch (cmd) {
            case CORE1_CMD_IDLE:
                current_task = CORE1_TASK_IDLE;
                previous_task = CORE1_TASK_IDLE;
                break;

            case CORE1_CMD_STOP:
                // Already handled by check_stop_requested during task
                // If we get here, we're already idle
                current_task = CORE1_TASK_IDLE;
                previous_task = CORE1_TASK_IDLE;
                break;

            case CORE1_CMD_PLAY_FSEQ:
                current_task = CORE1_TASK_FSEQ;
                fseq_loop_count = 0;
                run_fseq_task();
                previous_task = CORE1_TASK_FSEQ;  // Remember we were playing FSEQ
                current_task = CORE1_TASK_IDLE;
                break;

            case CORE1_CMD_PLAY_RAINBOW:
                current_task = CORE1_TASK_RAINBOW;
                run_rainbow_task();
                previous_task = CORE1_TASK_RAINBOW;
                current_task = CORE1_TASK_IDLE;
                break;

            default:
                printf("Core1: Unknown command %u\n", cmd);
                break;
        }

        // Signal that we're now idle (Core 0 might be waiting)
        __dmb();
    }
}

// --- API for Core 0 ---

void core1_task_init(fseq_player_t* fseq_ctx, rainbow_test_t* rainbow_ctx) {
    g_fseq_ctx = fseq_ctx;
    g_rainbow_ctx = rainbow_ctx;
    current_task = CORE1_TASK_IDLE;
    previous_task = CORE1_TASK_IDLE;
    stop_requested = false;
    fseq_loop_count = 0;
}

void core1_stop_and_wait(void) {
    // Signal stop via shared variable
    stop_requested = true;
    __dmb();

    // Wait for Core 1 to become idle
    while (current_task != CORE1_TASK_IDLE) {
        tight_loop_contents();
        __dmb();
    }
}

void core1_start_fseq(const char* filename) {
    // Stop any current task first
    core1_stop_and_wait();

    // Copy filename to shared buffer
    strncpy(g_filename, filename, sizeof(g_filename) - 1);
    g_filename[sizeof(g_filename) - 1] = '\0';

    // Send command via shared memory
    pending_cmd = CORE1_CMD_PLAY_FSEQ;
    __dmb();
    cmd_pending = true;
    __dmb();
}

void core1_start_rainbow(void) {
    // Stop any current task first
    core1_stop_and_wait();

    // Send command via shared memory
    pending_cmd = CORE1_CMD_PLAY_RAINBOW;
    __dmb();
    cmd_pending = true;
    __dmb();
}

core1_task_t core1_get_current_task(void) {
    __dmb();
    return current_task;
}

bool core1_is_idle(void) {
    __dmb();
    return current_task == CORE1_TASK_IDLE;
}

uint32_t core1_get_fseq_loop_count(void) {
    __dmb();
    return fseq_loop_count;
}

// Called by fseq_player when a loop completes
void core1_notify_fseq_loop(void) {
    fseq_loop_count++;
    __dmb();
}
