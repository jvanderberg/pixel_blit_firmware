#include "string_length_test.h"
#include "board_config.h"
#include "pico/stdlib.h"
#include <string.h>

// Store GPIO base for lazy driver init
static uint g_string_length_gpio_base = 0;

// Create driver on demand
static bool create_driver(string_length_test_t *ctx) {
    if (ctx->driver) return true;  // Already exists

    // Use color order from board_config (same as rainbow test)
    pb_color_order_t color_order = board_config_get_color_order(0);

    pb_driver_config_t config = {
        .board_id = 0,
        .num_boards = 1,
        .gpio_base = g_string_length_gpio_base,
        .num_strings = STRING_LENGTH_TEST_NUM_STRINGS,
        .max_pixel_length = STRING_LENGTH_TEST_MAX_PIXELS,
        .frequency_hz = 800000,
        .color_order = color_order,
        .reset_us = 200,
        .pio_index = 1,  // Use PIO1 (same as rainbow/fseq)
    };

    for (int i = 0; i < STRING_LENGTH_TEST_NUM_STRINGS; i++) {
        config.strings[i].length = STRING_LENGTH_TEST_MAX_PIXELS;
        config.strings[i].enabled = true;
    }

    ctx->driver = pb_driver_init(&config);
    if (!ctx->driver) {
        return false;
    }

    return true;
}

// Destroy driver to free PIO
static void destroy_driver(string_length_test_t *ctx) {
    if (ctx->driver) {
        pb_driver_deinit(ctx->driver);
        ctx->driver = NULL;
    }
}

bool string_length_test_init(string_length_test_t *ctx, uint first_pin) {
    if (!ctx) return false;

    memset(ctx, 0, sizeof(string_length_test_t));
    g_string_length_gpio_base = first_pin;
    ctx->driver = NULL;
    ctx->running = false;
    ctx->current_string = 0;
    ctx->current_pixel = 0;

    return true;
}

void string_length_test_start(string_length_test_t *ctx) {
    if (!ctx || ctx->running) return;

    if (!create_driver(ctx)) return;

    ctx->current_string = 0;
    ctx->current_pixel = 0;
    ctx->running = true;

    // Initial output
    string_length_test_update(ctx, 0, 0);
}

void string_length_test_stop(string_length_test_t *ctx) {
    if (!ctx) return;

    ctx->running = false;

    if (ctx->driver) {
        pb_show_wait(ctx->driver);
        pb_clear_all(ctx->driver, 0x000000);
        pb_show(ctx->driver);
        destroy_driver(ctx);
    }
}

void string_length_test_update(string_length_test_t *ctx, uint8_t string, uint16_t pixel) {
    if (!ctx || !ctx->running || !ctx->driver) return;

    ctx->current_string = string;
    ctx->current_pixel = pixel;

    // Clear all pixels
    pb_clear_all(ctx->driver, 0x000000);

    // Set a single red pixel at the current position
    pb_set_pixel(ctx->driver, 0, string, pixel, 0xFF0000);

    // Output
    pb_show(ctx->driver);
}

bool string_length_test_is_running(const string_length_test_t *ctx) {
    return ctx && ctx->running;
}
