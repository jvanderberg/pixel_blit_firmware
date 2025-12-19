#include "fseq_player.h"
#include "fseq_parser.h"
#include "board_config.h"
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "ff.h"
#include "sd_card.h"
#include "hw_config.h"
#include <stdio.h>
#include <string.h>

// Notify Core 1 task manager of loop completion (defined in core1_task.c)
extern void core1_notify_fseq_loop(void);

// File handle and parser state (persists across start/run_loop/cleanup)
static FIL g_fseq_file;
static bool g_file_open = false;
static fseq_parser_ctx_t *g_parser = NULL;
static fseq_header_t g_header;

// Parser layout - MUST be static so pointer remains valid after start() returns
static uint16_t g_string_lengths[BOARD_CONFIG_MAX_STRINGS];

// GPIO base for driver creation
static uint g_gpio_base = 0;

// Pixel callback - called by fseq_parser for each pixel
static void pixel_callback(void *user_data, uint8_t string, uint16_t pixel, uint32_t color) {
    fseq_player_t *ctx = (fseq_player_t *)user_data;
    uint16_t string_pixel_count = board_config_get_pixel_count(string);
    if (ctx && ctx->driver && string < FSEQ_PLAYER_MAX_STRINGS &&
        string_pixel_count > 0 && pixel < string_pixel_count) {
        pb_set_pixel(ctx->driver, 0, string, pixel, color);
    }
}

// Create driver with correct layout for playback using board_config
static bool create_driver(fseq_player_t *ctx) {
    if (ctx->driver) {
        return true;  // Already created
    }

    // Use board config for layout
    uint8_t num_strings = 0;
    uint16_t max_pixels = 0;

    // Count active strings and find max pixel count
    for (int i = 0; i < BOARD_CONFIG_MAX_STRINGS; i++) {
        uint16_t pixel_count = board_config_get_pixel_count(i);
        if (pixel_count > 0) {
            num_strings = i + 1;
            if (pixel_count > max_pixels) {
                max_pixels = pixel_count;
            }
        }
    }

    if (num_strings == 0 || max_pixels == 0) {
        printf("FSEQ: No strings configured in board_config\n");
        return false;
    }

    pb_driver_config_t config = {
        .board_id = g_board_config.board_id,
        .num_boards = 1,
        .gpio_base = g_gpio_base,
        .num_strings = num_strings,
        .max_pixel_length = max_pixels,
        .frequency_hz = 800000,
        .color_order = PB_COLOR_ORDER_RGB,  // Pass-through: let xLights handle color order
        .reset_us = 200,
        .pio_index = 1,  // Use PIO1
    };

    // Configure each string from board_config
    for (int i = 0; i < num_strings; i++) {
        uint16_t pixel_count = board_config_get_pixel_count(i);
        config.strings[i].length = pixel_count;
        config.strings[i].enabled = (pixel_count > 0);
    }

    ctx->driver = pb_driver_init(&config);
    if (ctx->driver == NULL) {
        printf("FSEQ: Failed to create pb_driver\n");
        return false;
    }

    printf("FSEQ: Driver created (%d strings, max %d pixels)\n",
           num_strings, max_pixels);
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

bool fseq_player_init(fseq_player_t *ctx, uint first_pin) {
    if (!ctx) {
        return false;
    }

    memset(ctx, 0, sizeof(fseq_player_t));
    g_gpio_base = first_pin;
    ctx->driver = NULL;
    ctx->running = false;

    printf("FSEQ: Player initialized\n");
    return true;
}

bool fseq_player_start(fseq_player_t *ctx, const char *filename) {
    if (!ctx) {
        return false;
    }

    // Create driver on demand
    if (!create_driver(ctx)) {
        return false;
    }

    // Copy filename
    strncpy(ctx->filename, filename, sizeof(ctx->filename) - 1);
    ctx->filename[sizeof(ctx->filename) - 1] = '\0';

    // Build full path
    char path[40];
    snprintf(path, sizeof(path), "/%s", ctx->filename);

    // Open file
    FRESULT fr = f_open(&g_fseq_file, path, FA_READ);
    if (fr != FR_OK) {
        printf("FSEQ: Failed to open %s (err %d)\n", path, fr);
        // Don't destroy driver - keep it for next file attempt
        return false;
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
        return false;
    }

    // Setup parser layout from board_config - using STATIC array
    uint8_t num_strings = 0;
    for (int i = 0; i < BOARD_CONFIG_MAX_STRINGS; i++) {
        g_string_lengths[i] = board_config_get_pixel_count(i);
        if (g_string_lengths[i] > 0) {
            num_strings = i + 1;
        }
    }

    fseq_layout_t layout = {
        .num_strings = num_strings,
        .string_lengths = g_string_lengths,  // Points to static array - safe!
    };

    // Initialize parser
    g_parser = fseq_parser_init(ctx, pixel_callback, layout);
    if (!g_parser) {
        printf("FSEQ: Failed to init parser\n");
        f_close(&g_fseq_file);
        g_file_open = false;
        return false;
    }

    // Parse header
    if (!fseq_parser_read_header(g_parser, header_buf, &g_header)) {
        printf("FSEQ: Invalid FSEQ header\n");
        fseq_parser_deinit(g_parser);
        g_parser = NULL;
        f_close(&g_fseq_file);
        g_file_open = false;
        return false;
    }

    printf("FSEQ: %s - %lu frames @ %d fps\n",
           ctx->filename,
           (unsigned long)g_header.frame_count,
           g_header.step_time_ms > 0 ? 1000 / g_header.step_time_ms : 0);

    // Calculate target FPS from step_time_ms
    ctx->target_fps = g_header.step_time_ms > 0 ? (1000 / g_header.step_time_ms) : 30;

    // Seek to channel data
    fr = f_lseek(&g_fseq_file, g_header.channel_data_offset);
    if (fr != FR_OK) {
        printf("FSEQ: Failed to seek to data\n");
        fseq_parser_deinit(g_parser);
        g_parser = NULL;
        f_close(&g_fseq_file);
        g_file_open = false;
        return false;
    }

    ctx->fps = 0;
    ctx->running = true;

    printf("FSEQ: Playback started\n");
    return true;
}

void fseq_player_run_loop(fseq_player_t *ctx, fseq_stop_check_fn stop_check) {
    if (!ctx || !ctx->running || !g_parser) {
        printf("FSEQ: run_loop early exit (ctx=%p, running=%d, parser=%p)\n",
               ctx, ctx ? ctx->running : 0, g_parser);
        return;
    }

    printf("FSEQ: run_loop starting, channel_count=%lu\n", (unsigned long)g_header.channel_count);

    // Playback loop - read in chunks, parser handles frame boundaries
    uint32_t frame_size = g_header.channel_count;
    uint8_t buffer[512];
    if (frame_size > sizeof(buffer)) frame_size = sizeof(buffer);

    uint32_t frames_played = 0;
    uint32_t fps_frame_count = 0;
    uint64_t last_fps_time = time_us_64();
    UINT bytes_read;
    FRESULT fr;

    printf("FSEQ: Entering playback loop, frame_size=%lu, channel_count=%lu\n",
           (unsigned long)frame_size, (unsigned long)g_header.channel_count);

    uint32_t read_count = 0;
    uint32_t total_bytes_read = 0;

    while (true) {
        // Check if we should stop
        if (stop_check && stop_check()) {
            printf("FSEQ: Stop requested after %lu reads, %lu bytes, %lu frames\n",
                   (unsigned long)read_count, (unsigned long)total_bytes_read,
                   (unsigned long)frames_played);
            break;
        }

        // Loop based on header frame count, not EOF
        if (frames_played >= g_header.frame_count) {
            // Notify that we completed a loop (for auto-advance)
            core1_notify_fseq_loop();

            f_lseek(&g_fseq_file, g_header.channel_data_offset);
            fseq_parser_reset(g_parser);
            frames_played = 0;
            read_count = 0;
            total_bytes_read = 0;
            continue;
        }

        fr = f_read(&g_fseq_file, buffer, frame_size, &bytes_read);
        if (fr != FR_OK || bytes_read < frame_size) {
            // Unexpected EOF or error - loop back
            printf("FSEQ: Read error or EOF, looping (fr=%d, bytes=%u)\n", fr, bytes_read);
            f_lseek(&g_fseq_file, g_header.channel_data_offset);
            fseq_parser_reset(g_parser);
            frames_played = 0;
            read_count = 0;
            total_bytes_read = 0;
            continue;
        }

        read_count++;
        total_bytes_read += bytes_read;

        // Check for stop request after potentially slow SD read
        if (stop_check && stop_check()) {
            printf("FSEQ: Stop after SD read (%lu reads, %lu bytes)\n",
                   (unsigned long)read_count, (unsigned long)total_bytes_read);
            break;
        }

        // Push data to parser
        bool frame_complete = fseq_parser_push(g_parser, buffer, bytes_read);

        if (frame_complete) {
            // Debug: log frame completion with byte counts
            if (frames_played < 3) {  // Only log first few frames
                printf("FSEQ: Frame %lu complete after %lu reads (%lu bytes)\n",
                       (unsigned long)frames_played, (unsigned long)read_count,
                       (unsigned long)total_bytes_read);
            }

            // Output the frame with FPS limiting
            pb_show_with_fps(ctx->driver, ctx->target_fps);
            frames_played++;
            fps_frame_count++;

            // Reset per-frame counters
            read_count = 0;
            total_bytes_read = 0;

            // Update FPS every second
            uint64_t now = time_us_64();
            if (now - last_fps_time >= 1000000) {
                ctx->fps = fps_frame_count;
                fps_frame_count = 0;
                last_fps_time = now;
            }
        }
    }
}

void fseq_player_cleanup(fseq_player_t *ctx) {
    if (!ctx) return;

    printf("FSEQ: Cleaning up (keeping driver)\n");

    // Clean up parser
    if (g_parser) {
        fseq_parser_deinit(g_parser);
        g_parser = NULL;
    }

    // Close file
    if (g_file_open) {
        f_close(&g_fseq_file);
        g_file_open = false;
    }

    // Clear LEDs but keep driver for fast file switching
    if (ctx->driver) {
        pb_show_wait(ctx->driver);      // Wait for any previous DMA
        pb_clear_all(ctx->driver, 0x000000);
        pb_show(ctx->driver);           // Start DMA to output cleared frame
        pb_show_wait(ctx->driver);      // Wait for DMA to complete
    }

    ctx->running = false;
}

void fseq_player_shutdown(fseq_player_t *ctx) {
    if (!ctx) return;

    // Clean up file/parser first
    fseq_player_cleanup(ctx);

    // Now destroy driver
    if (ctx->driver) {
        destroy_driver(ctx);
    }

    printf("FSEQ: Shutdown complete\n");
}

bool fseq_player_is_running(const fseq_player_t *ctx) {
    return ctx && ctx->running;
}

uint16_t fseq_player_get_fps(const fseq_player_t *ctx) {
    return ctx ? ctx->fps : 0;
}
