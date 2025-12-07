#include "fseq_parser.h"
#include <stdlib.h>
#include <string.h>

/**
 * @brief Internal context structure.
 */
struct fseq_parser_ctx {
    void* user_data;            ///< User context passed to callback
    fseq_pixel_cb pixel_cb;     ///< Callback function
    fseq_layout_t layout;       ///< Hardware layout for mapping
    fseq_header_t header;       ///< Cached file header
    
    // Streaming State
    uint32_t current_channel_index; ///< Current channel offset within the frame (0 to channel_count)
    uint8_t  temp_pixel[3];         ///< Buffer for assembling split pixels (R, G, B)
    uint8_t  temp_pixel_idx;        ///< Number of bytes currently in temp_pixel (0, 1, 2)
    bool     frame_completed;       ///< Flag to indicate frame end was reached
};

fseq_parser_ctx_t* fseq_parser_init(void* user_data, fseq_pixel_cb pixel_cb, fseq_layout_t layout) {
    if (!pixel_cb) return NULL;
    
    fseq_parser_ctx_t* ctx = malloc(sizeof(fseq_parser_ctx_t));
    if (ctx) {
        memset(ctx, 0, sizeof(fseq_parser_ctx_t));
        ctx->user_data = user_data;
        ctx->pixel_cb = pixel_cb;
        ctx->layout = layout;
    }
    return ctx;
}

void fseq_parser_deinit(fseq_parser_ctx_t* ctx) {
    free(ctx);
}

void fseq_parser_reset(fseq_parser_ctx_t* ctx) {
    if (!ctx) return;
    // Reset channel index to 0 (start of frame)
    ctx->current_channel_index = 0;
    // Clear any partial pixel data
    ctx->temp_pixel_idx = 0;
    ctx->frame_completed = false;
}

bool fseq_parser_read_header(fseq_parser_ctx_t* ctx, const uint8_t* buffer, fseq_header_t* header_out) {
    if (!ctx || !buffer || !header_out) return false;

    memcpy(&ctx->header, buffer, 32);
    memcpy(header_out, &ctx->header, 32);

    // Check Magic 'PSEQ' (0x51455350 Little Endian)
    if (ctx->header.magic != 0x51455350) return false;
    // Check Version 2.0
    if (ctx->header.major_version != 2) return false;

    return true;
}

bool fseq_parser_push(fseq_parser_ctx_t* ctx, const uint8_t* data, uint32_t len) {
    if (!ctx || !data) return false;
    
    bool any_frame_completed = false;

    for (uint32_t i = 0; i < len; i++) {
        // State Machine: Accumulate bytes into temp_pixel
        // This handles cases where a chunk ends with a partial pixel (e.g. just R, or R+G)
        ctx->temp_pixel[ctx->temp_pixel_idx++] = data[i];

        // If we have collected 3 bytes, we have a full pixel (R, G, B)
        if (ctx->temp_pixel_idx == 3) {
            // 1. Form color (Assumes RGB order in file)
            uint32_t color = (ctx->temp_pixel[0] << 16) | 
                             (ctx->temp_pixel[1] << 8)  | 
                             ctx->temp_pixel[2];

            // 2. Calculate Hardware Address
            // FSEQ data is linear channels: [S0P0R, S0P0G, S0P0B, S0P1R...]
            // 3 channels = 1 pixel.
            // We map linear channel index to (String, Pixel) based on configured layout.
            
            uint32_t channels_per_string = ctx->layout.pixels_per_string * 3;
            
            if (channels_per_string > 0) {
                // Integer division for string index
                uint8_t string = (uint8_t)(ctx->current_channel_index / channels_per_string);
                // Modulo for pixel index within that string
                uint16_t pixel = (uint16_t)((ctx->current_channel_index / 3) % ctx->layout.pixels_per_string);

                // Only output if the calculated address is within the board's capacity
                if (string < ctx->layout.num_strings) {
                    ctx->pixel_cb(ctx->user_data, string, pixel, color);
                }
            }

            // 3. Advance State
            ctx->temp_pixel_idx = 0; // Reset pixel buffer
            ctx->current_channel_index += 3; // Advance channel counter
        }

        // Frame Boundary Check
        // If we have processed all channels expected for this frame
        if (ctx->header.channel_count > 0 && ctx->current_channel_index >= ctx->header.channel_count) {
            ctx->current_channel_index = 0; // Wrap around for next frame
            ctx->temp_pixel_idx = 0;        // Safety reset (should be aligned)
            any_frame_completed = true;     // Signal caller to trigger show()
        }
    }

    return any_frame_completed;
}