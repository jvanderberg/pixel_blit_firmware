#include "rainbow_test.h"
#include "pico/stdlib.h"
#include <string.h>

#define RAINBOW_TARGET_FPS 120

bool rainbow_test_init(rainbow_test_t *ctx, uint first_pin)
{
    if (!ctx)
    {
        return false;
    }

    memset(ctx, 0, sizeof(rainbow_test_t));

    // Configure driver: 32 strings x 50 pixels
    pb_driver_config_t config = {
        .board_id = 0,
        .num_boards = 1,
        .gpio_base = first_pin,
        .num_strings = RAINBOW_TEST_NUM_STRINGS,
        .max_pixel_length = RAINBOW_TEST_PIXELS_PER_STRING,
        .frequency_hz = 800000,
        .color_order = PB_COLOR_ORDER_BRG,
        .reset_us = 200,
        .pio_index = 1,  // Use PIO1 to avoid conflict with other code
    };

    for (int i = 0; i < RAINBOW_TEST_NUM_STRINGS; i++)
    {
        config.strings[i].length = RAINBOW_TEST_PIXELS_PER_STRING;
        config.strings[i].enabled = true;
    }

    ctx->driver = pb_driver_init(&config);
    if (ctx->driver == NULL)
    {
        return false;
    }

    // Create 50x32 raster (width=pixels, height=strings)
    pb_raster_config_t raster_cfg = {
        .width = RAINBOW_TEST_PIXELS_PER_STRING,
        .height = RAINBOW_TEST_NUM_STRINGS,
        .board = 0,
        .start_string = 0,
        .start_pixel = 0,
        .wrap_mode = PB_WRAP_CLIP,  // Each row = one string
    };

    ctx->raster_id = pb_raster_create(ctx->driver, &raster_cfg);
    if (ctx->raster_id < 0)
    {
        pb_driver_deinit(ctx->driver);
        ctx->driver = NULL;
        return false;
    }

    ctx->raster = pb_raster_get(ctx->driver, ctx->raster_id);
    ctx->running = false;
    ctx->current_string = 0;
    ctx->hue_offset = 0;

    return true;
}

void rainbow_test_start(rainbow_test_t *ctx)
{
    if (!ctx || !ctx->driver || ctx->running)
    {
        return;
    }

    ctx->hue_offset = 0;
    ctx->frame_count = 0;
    ctx->fps = 0;
    ctx->running = true;
}

void rainbow_test_stop(rainbow_test_t *ctx)
{
    if (!ctx || !ctx->driver)
    {
        return;
    }

    if (ctx->running)
    {
        ctx->running = false;

        // Wait for any in-progress DMA to complete
        pb_show_wait(ctx->driver);

        // Clear all pixels
        pb_raster_fill(ctx->raster, 0x000000);
        pb_raster_show(ctx->driver, ctx->raster);
        pb_show(ctx->driver);
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

    // Fill background with dim white
    pb_raster_fill(ctx->raster, 0x101010);

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
