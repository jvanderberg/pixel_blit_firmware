#include "fseq_player.h"
#include "fseq_parser.h"
#include "pico/stdlib.h"
#include "hardware/sync.h"  // For __dmb() memory barrier
#include "ff.h"
#include "sd_card.h"
#include "hw_config.h"
#include <stdio.h>
#include <string.h>

// Global context pointer for Core 1 access
static fseq_player_t *g_player_ctx = NULL;

// External flag set by side_effects.c to signal stop
extern volatile bool fseq_core1_running;

// Global file handle so we can close it if Core 1 is reset
static FIL g_fseq_file;
static bool g_file_open = false;

// Pixel callback - called by fseq_parser for each pixel
static void pixel_callback(void *user_data, uint8_t string, uint16_t pixel, uint32_t color) {
    fseq_player_t *ctx = (fseq_player_t *)user_data;
    if (ctx && ctx->driver && string < FSEQ_PLAYER_NUM_STRINGS && pixel < FSEQ_PLAYER_PIXELS_PER_STRING) {
        pb_set_pixel(ctx->driver, 0, string, pixel, color);
    }
}

// Store GPIO base for lazy driver init
static uint g_gpio_base = 0;

bool fseq_player_init(fseq_player_t *ctx, uint first_pin) {
    if (!ctx) {
        return false;
    }

    memset(ctx, 0, sizeof(fseq_player_t));

    // Store config for lazy init - driver created in start()
    g_gpio_base = first_pin;
    ctx->driver = NULL;
    ctx->running = false;
    ctx->stop_requested = false;
    g_player_ctx = ctx;

    printf("FSEQ: Player initialized (driver created on demand)\n");
    return true;
}

// Create driver with correct layout for playback
static bool create_driver(fseq_player_t *ctx) {
    if (ctx->driver) {
        return true;  // Already created
    }

    pb_driver_config_t config = {
        .board_id = 0,
        .num_boards = 1,
        .gpio_base = g_gpio_base,
        .num_strings = FSEQ_PLAYER_NUM_STRINGS,
        .max_pixel_length = FSEQ_PLAYER_PIXELS_PER_STRING,
        .frequency_hz = 800000,
        .color_order = PB_COLOR_ORDER_RGB,  // Pass-through: let xLights handle color order
        .reset_us = 200,
        .pio_index = 1,  // Use PIO1
    };

    for (int i = 0; i < FSEQ_PLAYER_NUM_STRINGS; i++) {
        config.strings[i].length = FSEQ_PLAYER_PIXELS_PER_STRING;
        config.strings[i].enabled = true;
    }

    ctx->driver = pb_driver_init(&config);
    if (ctx->driver == NULL) {
        printf("FSEQ: Failed to create pb_driver\n");
        return false;
    }

    printf("FSEQ: Driver created (%d strings x %d pixels)\n",
           FSEQ_PLAYER_NUM_STRINGS, FSEQ_PLAYER_PIXELS_PER_STRING);
    return true;
}

// Destroy driver to free PIO resources
static void destroy_driver(fseq_player_t *ctx) {
    if (ctx->driver) {
        pb_driver_deinit(ctx->driver);
        ctx->driver = NULL;
        printf("FSEQ: Driver destroyed\n");
    }
}

bool fseq_player_start(fseq_player_t *ctx, const char *filename) {
    if (!ctx || ctx->running) {
        return false;
    }

    // Create driver on demand
    if (!create_driver(ctx)) {
        return false;
    }

    // Copy filename
    strncpy(ctx->filename, filename, sizeof(ctx->filename) - 1);
    ctx->filename[sizeof(ctx->filename) - 1] = '\0';

    ctx->stop_requested = false;
    ctx->fps = 0;
    ctx->running = true;

    printf("FSEQ: Starting playback of %s\n", ctx->filename);
    return true;
}

void fseq_player_stop(fseq_player_t *ctx) {
    if (!ctx) return;

    ctx->stop_requested = true;

    // Close file if Core 1 didn't get to do it
    if (g_file_open) {
        f_close(&g_fseq_file);
        g_file_open = false;
    }

    // Clean up driver
    if (ctx->driver) {
        pb_clear_all(ctx->driver, 0x000000);
        pb_show(ctx->driver);
        destroy_driver(ctx);
    }

    ctx->running = false;
}

bool fseq_player_is_running(const fseq_player_t *ctx) {
    return ctx && ctx->running;
}

uint16_t fseq_player_get_fps(const fseq_player_t *ctx) {
    return ctx ? ctx->fps : 0;
}

// Core 1 entry point - runs the full playback loop
void fseq_player_core1_entry(void) {
    fseq_player_t *ctx = g_player_ctx;

    if (!ctx || !ctx->running) {
        return;
    }

    printf("FSEQ Core1: Starting %s\n", ctx->filename);

    // Build full path
    char path[32];
    snprintf(path, sizeof(path), "/%s", ctx->filename);

    // Open file
    FRESULT fr = f_open(&g_fseq_file, path, FA_READ);
    if (fr != FR_OK) {
        printf("FSEQ: Failed to open %s\n", path);
        ctx->running = false;
        return;
    }
    g_file_open = true;

    // Read and parse header
    uint8_t header_buf[32];
    UINT bytes_read;
    fr = f_read(&g_fseq_file, header_buf, 32, &bytes_read);
    if (fr != FR_OK || bytes_read != 32) {
        printf("FSEQ: Failed to read header\n");
        f_close(&g_fseq_file);
        g_file_open = false;
        ctx->running = false;
        return;
    }

    // Setup parser layout
    uint16_t string_lengths[FSEQ_PLAYER_NUM_STRINGS];
    for (int i = 0; i < FSEQ_PLAYER_NUM_STRINGS; i++) {
        string_lengths[i] = FSEQ_PLAYER_PIXELS_PER_STRING;
    }

    fseq_layout_t layout = {
        .num_strings = FSEQ_PLAYER_NUM_STRINGS,
        .string_lengths = string_lengths,
    };

    // Initialize parser
    fseq_parser_ctx_t *parser = fseq_parser_init(ctx, pixel_callback, layout);
    if (!parser) {
        printf("FSEQ Core1: Failed to init parser\n");
        f_close(&g_fseq_file);
        g_file_open = false;
        ctx->running = false;
        return;
    }

    // Parse header
    fseq_header_t header;
    if (!fseq_parser_read_header(parser, header_buf, &header)) {
        printf("FSEQ Core1: Invalid FSEQ header\n");
        fseq_parser_deinit(parser);
        f_close(&g_fseq_file);
        g_file_open = false;
        ctx->running = false;
        return;
    }

    printf("FSEQ: %lu frames @ %d fps\n",
           (unsigned long)header.frame_count,
           header.step_time_ms > 0 ? 1000 / header.step_time_ms : 0);

    // Calculate target FPS from step_time_ms
    ctx->target_fps = header.step_time_ms > 0 ? (1000 / header.step_time_ms) : 30;

    // Seek to channel data
    fr = f_lseek(&g_fseq_file, header.channel_data_offset);
    if (fr != FR_OK) {
        printf("FSEQ Core1: Failed to seek to data\n");
        fseq_parser_deinit(parser);
        f_close(&g_fseq_file);
        g_file_open = false;
        ctx->running = false;
        return;
    }

    // Playback loop - read exactly one frame at a time for proper timing
    uint32_t frame_size = header.channel_count;
    uint8_t buffer[512];  // Max frame size we support
    if (frame_size > sizeof(buffer)) frame_size = sizeof(buffer);

    uint32_t frames_played = 0;
    uint32_t fps_frame_count = 0;
    uint64_t last_fps_time = time_us_64();

    while (true) {
        __dmb();  // Ensure we see latest flag value from Core0
        if (!fseq_core1_running || ctx->stop_requested) {
            break;
        }

        // Loop based on header frame count, not EOF
        if (frames_played >= header.frame_count) {
            f_lseek(&g_fseq_file, header.channel_data_offset);
            fseq_parser_reset(parser);
            frames_played = 0;
            continue;
        }

        fr = f_read(&g_fseq_file, buffer, frame_size, &bytes_read);
        if (fr != FR_OK || bytes_read < frame_size) {
            // Unexpected EOF or error - loop back
            f_lseek(&g_fseq_file, header.channel_data_offset);
            fseq_parser_reset(parser);
            frames_played = 0;
            continue;
        }

        // Check for stop request after potentially slow SD read
        __dmb();
        if (!fseq_core1_running || ctx->stop_requested) {
            break;
        }

        // Push data to parser
        bool frame_complete = fseq_parser_push(parser, buffer, bytes_read);

        if (frame_complete) {
            // Output the frame with FPS limiting
            pb_show_with_fps(ctx->driver, ctx->target_fps);
            frames_played++;
            fps_frame_count++;

            // Update FPS every second
            uint64_t now = time_us_64();
            if (now - last_fps_time >= 1000000) {
                ctx->fps = fps_frame_count;
                fps_frame_count = 0;
                last_fps_time = now;
            }
        }
    }

    printf("FSEQ Core1: Stopping playback\n");

    // Cleanup
    fseq_parser_deinit(parser);
    f_close(&g_fseq_file);
    g_file_open = false;

    // Clear LEDs and destroy driver to free PIO
    pb_clear_all(ctx->driver, 0x000000);
    pb_show(ctx->driver);
    destroy_driver(ctx);

    ctx->running = false;
    printf("FSEQ Core1: Playback stopped\n");
}
