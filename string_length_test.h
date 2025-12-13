#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "pico/types.h"
#include "pb_led_driver.h"

#define STRING_LENGTH_TEST_NUM_STRINGS 32
#define STRING_LENGTH_TEST_MAX_PIXELS 512  // Must match PB_MAX_PIXELS

typedef struct {
    pb_driver_t *driver;
    bool running;
    uint8_t current_string;
    uint16_t current_pixel;
} string_length_test_t;

// Initialize (lazy driver creation)
bool string_length_test_init(string_length_test_t *ctx, uint first_pin);

// Start the test (creates driver)
void string_length_test_start(string_length_test_t *ctx);

// Stop the test (destroys driver)
void string_length_test_stop(string_length_test_t *ctx);

// Update the current position (called when state changes)
void string_length_test_update(string_length_test_t *ctx, uint8_t string, uint16_t pixel);

// Check if running
bool string_length_test_is_running(const string_length_test_t *ctx);
