#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "pb_led_driver.h"

// Maximum strings per board
#define BOARD_CONFIG_MAX_STRINGS 32

// Per-string configuration from config.csv
typedef struct {
    uint16_t pixel_count;         // Number of pixels (0 = disabled)
    pb_color_order_t color_order; // RGB, GRB, etc.
} string_config_t;

// Board configuration loaded from SD card
typedef struct {
    bool loaded;                  // True if config was successfully loaded
    uint8_t board_id;             // This board's ID (from ADC)
    uint8_t string_count;         // Highest configured string index + 1
    uint16_t max_pixel_count;     // Maximum pixels across all strings
    string_config_t strings[BOARD_CONFIG_MAX_STRINGS];
} board_config_t;

// Result of parsing a buffer
typedef struct {
    bool success;
    uint16_t error_line;          // 1-indexed line number (0 if N/A)
    const char* error_msg;        // Error description (NULL if success)
} board_config_parse_result_t;

// Result of loading from SD
typedef struct {
    bool success;
    const char* error_msg;        // Error description (NULL if success)
} board_config_load_result_t;

// Global board configuration (populated at startup)
extern board_config_t g_board_config;

// ============================================================================
// Pure parsing functions (no I/O, testable on host)
// ============================================================================

// Parse color order string (e.g., "GRB", "RGB") to enum
pb_color_order_t board_config_parse_color_order(const char* str);

// Parse a single CSV line: "pixel_count,color_order"
// Returns true if valid, false if empty/comment/invalid
bool board_config_parse_line(const char* line, uint16_t* pixel_count, pb_color_order_t* color_order);

// Parse entire config buffer for a specific board
// board_id determines which 32-row section to read (0=rows 0-31, 1=rows 32-63, etc.)
board_config_parse_result_t board_config_parse_buffer(
    const char* buffer,
    size_t buffer_len,
    uint8_t board_id,
    board_config_t* config
);

// Set default configuration (all 32 strings, 50 pixels, GRB)
void board_config_set_defaults(uint8_t board_id);

// Get color order for a specific string
// Returns GRB if string not configured
pb_color_order_t board_config_get_color_order(uint8_t string);

// Get pixel count for a specific string
// Returns 0 if string not configured
uint16_t board_config_get_pixel_count(uint8_t string);

// ============================================================================
// Hardware-dependent functions (target only)
// ============================================================================

#ifndef BOARD_CONFIG_TEST_BUILD

// Load configuration from SD card config.csv
// Returns result with error message if failed
board_config_load_result_t board_config_load_from_sd(uint8_t board_id);

#endif
