#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "pico/types.h"
#include "pb_led_driver.h"
#include "board_config.h"

// Maximum supported layout (from board_config.h)
#define FSEQ_PLAYER_MAX_STRINGS BOARD_CONFIG_MAX_STRINGS

typedef struct {
    pb_driver_t *driver;
    volatile bool running;
    volatile bool stop_requested;
    char filename[13];  // 8.3 + null
    uint16_t target_fps;
    uint16_t fps;
} fseq_player_t;

// Initialize the player (creates pb_driver with correct layout)
bool fseq_player_init(fseq_player_t *ctx, uint first_pin);

// Start playback of a file (sets up state, Core 1 runs the loop)
bool fseq_player_start(fseq_player_t *ctx, const char *filename);

// Stop playback
void fseq_player_stop(fseq_player_t *ctx);

// Core 1 entry point - runs the playback loop
void fseq_player_core1_entry(void);

// Check if running
bool fseq_player_is_running(const fseq_player_t *ctx);

// Get current FPS
uint16_t fseq_player_get_fps(const fseq_player_t *ctx);
