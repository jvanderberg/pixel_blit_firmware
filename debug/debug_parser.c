/**
 * debug_parser.c - Host-based debug tool for config.csv and FSEQ parsing
 *
 * Build: gcc -o debug_parser debug_parser.c \
 *        ../src/board_config.c \
 *        ../lib/fseq_parser/src/fseq_parser.c \
 *        -I../src -I../lib/fseq_parser/include -I../lib/pb_led_driver \
 *        -DBOARD_CONFIG_TEST_BUILD -DPB_LED_DRIVER_TEST_BUILD
 *
 * Usage: ./debug_parser [board_id]
 *        Looks for config.csv and test.fseq in the current directory (debug/)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

// Include the headers we need
#include "board_config.h"
#include "fseq_parser.h"

// Color order names for display
static const char* color_order_name(pb_color_order_t order) {
    switch (order) {
        case PB_COLOR_ORDER_RGB: return "RGB";
        case PB_COLOR_ORDER_GRB: return "GRB";
        case PB_COLOR_ORDER_BGR: return "BGR";
        case PB_COLOR_ORDER_RBG: return "RBG";
        case PB_COLOR_ORDER_GBR: return "GBR";
        case PB_COLOR_ORDER_BRG: return "BRG";
        default: return "???";
    }
}

// Stats for FSEQ parsing
typedef struct {
    uint32_t total_pixels;
    uint32_t frames_completed;
    uint32_t pixels_per_string[32];
    uint32_t last_frame_pixels;
    uint32_t current_frame_pixels;  // Track pixels in current frame
    uint8_t last_string;
    uint16_t last_pixel;
} parse_stats_t;

static parse_stats_t stats;

// Pixel callback - collect stats and optionally print
static void pixel_callback(void* user_data, uint8_t string, uint16_t pixel, uint32_t color) {
    (void)user_data;

    // Detect frame boundary by string/pixel reset
    if (string == 0 && pixel == 0 && stats.total_pixels > 0) {
        // New frame started - the previous frame had current_frame_pixels
        stats.last_frame_pixels = stats.current_frame_pixels;
        stats.current_frame_pixels = 0;
    }

    stats.total_pixels++;
    stats.current_frame_pixels++;

    if (string < 32) {
        stats.pixels_per_string[string]++;
    }

    stats.last_string = string;
    stats.last_pixel = pixel;

    // Print first few pixels of first frame for verification
    if (stats.frames_completed == 0 && stats.current_frame_pixels <= 5) {
        printf("  [%u] string=%u pixel=%u color=#%06X\n",
               stats.current_frame_pixels - 1, string, pixel, color);
    }
}

// Load file into buffer
static char* load_file(const char* path, size_t* size_out) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* buffer = malloc(size + 1);
    if (!buffer) {
        fclose(f);
        return NULL;
    }

    size_t read = fread(buffer, 1, size, f);
    fclose(f);

    buffer[read] = '\0';
    *size_out = read;
    return buffer;
}

int main(int argc, char* argv[]) {
    uint8_t board_id = 0;

    if (argc > 1) {
        board_id = (uint8_t)atoi(argv[1]);
    }

    printf("=== Pixel Blit Debug Parser ===\n\n");
    printf("Board ID: %u\n\n", board_id);

    // ========================================================================
    // Load and parse config.csv
    // ========================================================================

    printf("--- Loading config.csv ---\n");

    size_t config_size;
    char* config_buffer = load_file("config.csv", &config_size);

    if (!config_buffer) {
        printf("ERROR: Could not open config.csv\n");
        printf("Make sure config.csv exists in the debug/ directory\n\n");
    } else {
        printf("Loaded %zu bytes\n\n", config_size);

        // Parse it
        board_config_t config;
        board_config_parse_result_t result = board_config_parse_buffer(
            config_buffer, config_size, board_id, &config
        );

        if (!result.success) {
            printf("PARSE ERROR: %s", result.error_msg);
            if (result.error_line > 0) {
                printf(" (line %u)", result.error_line);
            }
            printf("\n\n");
        } else {
            printf("Config parsed successfully!\n");
            printf("  String count: %u\n", config.string_count);
            printf("  Max pixels: %u\n\n", config.max_pixel_count);

            printf("String Configuration:\n");
            printf("  %-8s %-8s %-8s\n", "String", "Pixels", "Color");
            printf("  %-8s %-8s %-8s\n", "------", "------", "-----");

            uint32_t total_channels = 0;
            for (int i = 0; i < config.string_count; i++) {
                if (config.strings[i].pixel_count > 0) {
                    printf("  %-8d %-8u %-8s\n",
                           i,
                           config.strings[i].pixel_count,
                           color_order_name(config.strings[i].color_order));
                    total_channels += config.strings[i].pixel_count * 3;
                }
            }
            printf("\n  Total channels: %u\n\n", total_channels);

            // Copy to global for FSEQ parsing
            memcpy(&g_board_config, &config, sizeof(config));
        }

        free(config_buffer);
    }

    // ========================================================================
    // Load and parse test.fseq
    // ========================================================================

    printf("--- Loading test.fseq ---\n");

    size_t fseq_size;
    char* fseq_buffer = load_file("test.fseq", &fseq_size);

    if (!fseq_buffer) {
        printf("ERROR: Could not open test.fseq\n");
        printf("Make sure test.fseq exists in the debug/ directory\n\n");
        return 1;
    }

    printf("Loaded %zu bytes\n\n", fseq_size);

    // Build layout from config
    uint16_t string_lengths[32];
    uint8_t num_strings = 0;

    if (g_board_config.loaded) {
        num_strings = g_board_config.string_count;
        for (int i = 0; i < num_strings; i++) {
            string_lengths[i] = g_board_config.strings[i].pixel_count;
        }
    } else {
        // Default: 32 strings x 50 pixels
        num_strings = 32;
        for (int i = 0; i < 32; i++) {
            string_lengths[i] = 50;
        }
        printf("Using default layout: 32 strings x 50 pixels\n\n");
    }

    fseq_layout_t layout = {
        .num_strings = num_strings,
        .string_lengths = string_lengths
    };

    // Debug: verify layout
    printf("Layout passed to parser:\n");
    printf("  num_strings: %u\n", layout.num_strings);
    for (int i = 0; i < layout.num_strings && i < 4; i++) {
        printf("  string_lengths[%d]: %u\n", i, layout.string_lengths[i]);
    }
    printf("\n");

    // Initialize parser
    memset(&stats, 0, sizeof(stats));

    fseq_parser_ctx_t* parser = fseq_parser_init(NULL, pixel_callback, layout);
    if (!parser) {
        printf("ERROR: Failed to initialize FSEQ parser\n");
        free(fseq_buffer);
        return 1;
    }

    // Parse header
    fseq_header_t header;
    if (!fseq_parser_read_header(parser, (uint8_t*)fseq_buffer, &header)) {
        printf("ERROR: Invalid FSEQ header\n");
        printf("  Magic: 0x%08X (expected 0x51455350 'PSEQ')\n", header.magic);
        printf("  Version: %u.%u (expected 2.x)\n", header.major_version, header.minor_version);
        fseq_parser_deinit(parser);
        free(fseq_buffer);
        return 1;
    }

    printf("FSEQ Header:\n");
    printf("  Version: %u.%u\n", header.major_version, header.minor_version);
    printf("  Channel count: %u\n", header.channel_count);
    printf("  Frame count: %u\n", header.frame_count);
    printf("  Step time: %u ms (%.1f fps)\n", header.step_time_ms, 1000.0f / header.step_time_ms);
    printf("  Compression: %u (%s)\n", header.compression_type,
           header.compression_type == 0 ? "none" : "compressed");
    printf("  Data offset: %u\n", header.channel_data_offset);
    printf("\n");

    // Calculate expected vs configured channels
    uint32_t configured_channels = 0;
    for (int i = 0; i < num_strings; i++) {
        configured_channels += string_lengths[i] * 3;
    }

    printf("Channel Analysis:\n");
    printf("  FSEQ channels: %u\n", header.channel_count);
    printf("  Config channels: %u\n", configured_channels);
    if (header.channel_count != configured_channels) {
        printf("  WARNING: Channel count mismatch!\n");
        if (header.channel_count > configured_channels) {
            printf("  FSEQ has %u extra channels that will be ignored\n",
                   header.channel_count - configured_channels);
        } else {
            printf("  Config expects %u more channels than FSEQ provides\n",
                   configured_channels - header.channel_count);
        }
    } else {
        printf("  OK: Channel counts match\n");
    }
    printf("\n");

    // Check for compression
    if (header.compression_type != 0) {
        printf("ERROR: Compressed FSEQ files are not supported\n");
        printf("Export from xLights with compression disabled (V2 Uncompressed)\n");
        fseq_parser_deinit(parser);
        free(fseq_buffer);
        return 1;
    }

    // Parse first few frames
    printf("Parsing frames...\n");
    printf("First 10 pixels of frame 0:\n");

    uint8_t* data_start = (uint8_t*)fseq_buffer + header.channel_data_offset;
    size_t data_len = fseq_size - header.channel_data_offset;

    // Process exactly max_frames complete frames
    uint32_t max_frames = 10;
    uint32_t bytes_for_frames = max_frames * header.channel_count;

    if (bytes_for_frames > data_len) {
        bytes_for_frames = (data_len / header.channel_count) * header.channel_count;
        max_frames = bytes_for_frames / header.channel_count;
    }

    // Process in chunks like we would from SD card
    size_t chunk_size = 512;
    size_t offset = 0;

    while (offset < bytes_for_frames) {
        size_t remaining = bytes_for_frames - offset;
        size_t this_chunk = (remaining < chunk_size) ? remaining : chunk_size;

        bool frame_done = fseq_parser_push(parser, data_start + offset, this_chunk);

        if (frame_done) {
            stats.frames_completed++;
        }

        offset += this_chunk;
    }

    uint32_t pixels_per_frame = header.channel_count / 3;

    printf("\nParsing Summary:\n");
    printf("  Frames parsed: %u / %u\n", stats.frames_completed, header.frame_count);
    printf("  Pixels per frame: %u\n", pixels_per_frame);
    printf("  Total pixels: %u\n", stats.total_pixels);

    printf("\nPixels per string:\n");
    for (int i = 0; i < num_strings; i++) {
        uint32_t per_frame = stats.frames_completed > 0 ? stats.pixels_per_string[i] / stats.frames_completed : 0;
        const char* status = (per_frame == string_lengths[i]) ? "OK" : "MISMATCH";
        printf("  String %2d: %u/frame (expected %u) %s\n",
               i, per_frame, string_lengths[i], status);
    }

    fseq_parser_deinit(parser);
    free(fseq_buffer);

    printf("\n=== Done ===\n");
    return 0;
}
