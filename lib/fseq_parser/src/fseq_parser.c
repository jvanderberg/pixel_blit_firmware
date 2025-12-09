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
    uint8_t  current_string_idx;    ///< Current string being populated
    uint16_t current_pixel_idx;     ///< Current pixel index within the string
    
    uint8_t  temp_pixel[3];         ///< Buffer for assembling split pixels (R, G, B)
    uint8_t  temp_pixel_idx;        ///< Number of bytes currently in temp_pixel (0, 1, 2)
    bool     frame_completed;       ///< Flag to indicate frame end was reached
};

fseq_parser_ctx_t* fseq_parser_init(void* user_data, fseq_pixel_cb pixel_cb, fseq_layout_t layout) {
    if (!pixel_cb) return NULL;
    // ... (rest is same)
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
    ctx->current_string_idx = 0;
    ctx->current_pixel_idx = 0;
    // Clear any partial pixel data
    ctx->temp_pixel_idx = 0;
    ctx->frame_completed = false;
}

bool fseq_parser_read_header(fseq_parser_ctx_t* ctx, const uint8_t* buffer, fseq_header_t* header_out) {
    // ... (same)
    if (!ctx || !buffer || !header_out) return false;

    memcpy(&ctx->header, buffer, 32);
    memcpy(header_out, &ctx->header, 32);

    if (ctx->header.magic != 0x51455350) return false;
    if (ctx->header.major_version != 2) return false;

    return true;
}

bool fseq_parser_push(fseq_parser_ctx_t* ctx, const uint8_t* data, uint32_t len) {
    if (!ctx || !data) return false;
    
    bool any_frame_completed = false;

    for (uint32_t i = 0; i < len; i++) {
        // State Machine: Accumulate bytes into temp_pixel
        ctx->temp_pixel[ctx->temp_pixel_idx++] = data[i];

        // If we have collected 3 bytes, we have a full pixel (R, G, B)
        if (ctx->temp_pixel_idx == 3) {
            // 1. Form color (Assumes RGB order in file - set xLights String Type to RGB)
            uint32_t color = (ctx->temp_pixel[0] << 16) |
                             (ctx->temp_pixel[1] << 8)  |
                             ctx->temp_pixel[2];

            // 2. Output using current coordinates
            // Only if we are within valid string bounds
            if (ctx->current_string_idx < ctx->layout.num_strings) {
                ctx->pixel_cb(ctx->user_data, ctx->current_string_idx, ctx->current_pixel_idx, color);
            }

            // 3. Advance Pixel/String Counters
            ctx->current_pixel_idx++;
            
            // Check if we reached end of current string
            if (ctx->current_string_idx < ctx->layout.num_strings) {
                if (ctx->current_pixel_idx >= ctx->layout.string_lengths[ctx->current_string_idx]) {
                    ctx->current_string_idx++;
                    ctx->current_pixel_idx = 0;
                }
            }

            // 4. Advance Global State
            ctx->temp_pixel_idx = 0;
            ctx->current_channel_index += 3;
        }

        // Frame Boundary Check
        if (ctx->header.channel_count > 0 && ctx->current_channel_index >= ctx->header.channel_count) {
            // Reset all counters for next frame
            ctx->current_channel_index = 0;
            ctx->current_string_idx = 0;
            ctx->current_pixel_idx = 0;
            ctx->temp_pixel_idx = 0;
            any_frame_completed = true;
        }
    }

    return any_frame_completed;
}