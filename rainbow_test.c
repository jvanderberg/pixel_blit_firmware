#include "rainbow_test.h"
#include "board_config.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>

#define RAINBOW_TARGET_FPS 120

// Store GPIO base for lazy driver init
static uint g_rainbow_gpio_base = 0;

// Create driver and raster on demand
// Rainbow test always uses 32 strings x 50 pixels for hardware discovery
// but uses color order from board_config for correct color display
static bool create_driver(rainbow_test_t *ctx)
{
    if (ctx->driver) return true;  // Already exists

    // Use first configured string's color order (or GRB as fallback)
    pb_color_order_t color_order = board_config_get_color_order(0);

    pb_driver_config_t config = {
        .board_id = 0,
        .num_boards = 1,
        .gpio_base = g_rainbow_gpio_base,
        .num_strings = RAINBOW_TEST_NUM_STRINGS,
        .max_pixel_length = RAINBOW_TEST_PIXELS_PER_STRING,
        .frequency_hz = 800000,
        .color_order = color_order,
        .reset_us = 200,
        .pio_index = 1,
    };

    for (int i = 0; i < RAINBOW_TEST_NUM_STRINGS; i++) {
        config.strings[i].length = RAINBOW_TEST_PIXELS_PER_STRING;
        config.strings[i].enabled = true;
    }

    ctx->driver = pb_driver_init(&config);
    if (!ctx->driver) {
        printf("Rainbow: Failed to create driver\n");
        return false;
    }

    pb_raster_config_t raster_cfg = {
        .width = RAINBOW_TEST_PIXELS_PER_STRING,
        .height = RAINBOW_TEST_NUM_STRINGS,
        .board = 0,
        .start_string = 0,
        .start_pixel = 0,
        .wrap_mode = PB_WRAP_CLIP,
    };

    ctx->raster_id = pb_raster_create(ctx->driver, &raster_cfg);
    if (ctx->raster_id < 0) {
        pb_driver_deinit(ctx->driver);
        ctx->driver = NULL;
        printf("Rainbow: Failed to create raster\n");
        return false;
    }

    ctx->raster = pb_raster_get(ctx->driver, ctx->raster_id);
    printf("Rainbow: Driver created (color order: %d)\n", color_order);
    return true;
}

// Destroy driver to free PIO
static void destroy_driver(rainbow_test_t *ctx)
{
    if (ctx->driver) {
        pb_driver_deinit(ctx->driver);
        ctx->driver = NULL;
        ctx->raster = NULL;
        ctx->raster_id = -1;
        printf("Rainbow: Driver destroyed\n");
    }
}

bool rainbow_test_init(rainbow_test_t *ctx, uint first_pin)
{
    if (!ctx) return false;

    memset(ctx, 0, sizeof(rainbow_test_t));
    g_rainbow_gpio_base = first_pin;
    ctx->driver = NULL;
    ctx->raster = NULL;
    ctx->raster_id = -1;
    ctx->running = false;
    ctx->current_string = 0;
    ctx->hue_offset = 0;

    printf("Rainbow: Init (lazy driver)\n");
    return true;
}

void rainbow_test_start(rainbow_test_t *ctx)
{
    if (!ctx || ctx->running) return;

    if (!create_driver(ctx)) return;

    ctx->hue_offset = 0;
    ctx->frame_count = 0;
    ctx->fps = 0;
    ctx->running = true;
}

void rainbow_test_stop(rainbow_test_t *ctx)
{
    if (!ctx || !ctx->running) return;

    ctx->running = false;

    if (ctx->driver) {
        pb_show_wait(ctx->driver);
        pb_raster_fill(ctx->raster, 0x000000);
        pb_raster_show(ctx->driver, ctx->raster);
        pb_show(ctx->driver);
        destroy_driver(ctx);
    }
}

bool rainbow_test_is_running(const rainbow_test_t *ctx)
{
    return ctx && ctx->running;
}

void rainbow_test_next_string(rainbow_test_t *ctx)
{
    if (!ctx || !ctx->running)
    {
        return;
    }

    ctx->current_string = (ctx->current_string + 1) % RAINBOW_TEST_NUM_STRINGS;
}

uint8_t rainbow_test_get_string(const rainbow_test_t *ctx)
{
    return ctx ? ctx->current_string : 0;
}

void rainbow_test_task(rainbow_test_t *ctx)
{
    if (!ctx || !ctx->running || !ctx->raster)
    {
        return;
    }

    // Fill background with red (to test color order)
    pb_raster_fill(ctx->raster, 0xFF0000);

    // Draw rainbow on selected string
    uint8_t y = ctx->current_string;
    for (uint16_t x = 0; x < RAINBOW_TEST_PIXELS_PER_STRING; x++)
    {
        // Spread hue across string, offset animates the rainbow
        uint8_t hue = (uint8_t)((x * 255 / RAINBOW_TEST_PIXELS_PER_STRING) + ctx->hue_offset);
        pb_color_t color = pb_color_hsv(hue, 255, 64);  // Full saturation, moderate brightness
        pb_raster_set_pixel(ctx->raster, x, y, color);
    }

    // Animate rainbow
    ctx->hue_offset += 2;

    // Copy raster to LED buffer and output with FPS limiting
    pb_raster_show(ctx->driver, ctx->raster);
    pb_show_with_fps(ctx->driver, RAINBOW_TARGET_FPS);

    // Use driver's FPS tracking
    ctx->fps = pb_get_fps(ctx->driver);
}

uint16_t rainbow_test_get_fps(const rainbow_test_t *ctx)
{
    return ctx ? ctx->fps : 0;
}
