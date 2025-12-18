#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "pico/types.h"
#include "pb_led_driver.h"
#include "board_config.h"

// Maximum supported layout (from board_config.h)
#define FSEQ_PLAYER_MAX_STRINGS BOARD_CONFIG_MAX_STRINGS

// Stop check callback type - returns true if playback should stop
typedef bool (*fseq_stop_check_fn)(void);

typedef struct {
    pb_driver_t *driver;
    volatile bool running;
    char filename[32];  // Support long filenames
    uint16_t target_fps;
    uint16_t fps;
} fseq_player_t;

// Initialize the player context
bool fseq_player_init(fseq_player_t *ctx, uint first_pin);

// Start playback of a file (opens file, creates driver)
// Called from Core 1 task manager
bool fseq_player_start(fseq_player_t *ctx, const char *filename);

// Run the playback loop until stop_check returns true
// Called from Core 1 task manager
void fseq_player_run_loop(fseq_player_t *ctx, fseq_stop_check_fn stop_check);

// Clean up after playback (close file, clear LEDs, but keep driver for fast file switching)
// Called from Core 1 task manager
void fseq_player_cleanup(fseq_player_t *ctx);

// Full shutdown including driver destruction (call when switching to different task type)
void fseq_player_shutdown(fseq_player_t *ctx);

// Check if running
bool fseq_player_is_running(const fseq_player_t *ctx);

// Get current FPS
uint16_t fseq_player_get_fps(const fseq_player_t *ctx);
