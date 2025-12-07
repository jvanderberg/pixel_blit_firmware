/**
 * @file fseq_parser.h
 * @brief Streaming parser for xLights FSEQ v2 sequence files.
 */

#ifndef FSEQ_PARSER_H
#define FSEQ_PARSER_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief FSEQ v2 Header Structure (Fixed 32 bytes)
 * This matches the file specification exactly.
 * See: https://github.com/FalconChristmas/fpp/blob/master/docs/FSEQ_File_Format.txt
 */
typedef struct {
    uint32_t magic;             // 'PSEQ'
    uint16_t channel_data_offset;
    uint8_t  minor_version;
    uint8_t  major_version;     // Should be 2
    uint16_t header_length;
    uint32_t channel_count;     // Total channels per frame
    uint32_t frame_count;       // Total frames in file
    uint8_t  step_time_ms;      // Frame delay in milliseconds (e.g. 20ms = 50fps)
    uint8_t  flags;
    uint8_t  compression_type;  // 0=Uncompressed
    uint8_t  num_compression_blocks; // Lower 4 bits
    uint8_t  num_compression_blocks_high;
    uint8_t  num_sparse_ranges;
    uint8_t  reserved;
    uint64_t unique_id;
} __attribute__((packed)) fseq_header_t;

/**
 * @brief Opaque context for the streaming parser.
 */
typedef struct fseq_parser_ctx fseq_parser_ctx_t;

/**
 * @brief Callback for pixel output.
 * 
 * The parser invokes this function immediately when 3 bytes (R,G,B) have been processed.
 * 
 * @param user_data User pointer provided at init.
 * @param string The derived string index (0 to num_strings-1).
 * @param pixel The derived pixel index within the string.
 * @param color The 24-bit RGB color (0x00RRGGBB).
 */
typedef void (*fseq_pixel_cb)(void* user_data, uint8_t string, uint16_t pixel, uint32_t color);

// Configuration for the parser layout
typedef struct {
    uint8_t   num_strings;
    uint16_t* string_lengths; // Array of length [num_strings] containing pixel count for each string
} fseq_layout_t;

/**
 * @brief Initialize the parser context.
 * 
 * @param user_data Pointer passed to the pixel callback.
 * @param pixel_cb Function to call when a pixel is ready.
 * @param layout Hardware layout configuration for address mapping.
 * @return fseq_parser_ctx_t* Allocated context or NULL on failure.
 */
fseq_parser_ctx_t* fseq_parser_init(void* user_data, fseq_pixel_cb pixel_cb, fseq_layout_t layout);

/**
 * @brief Free the parser context.
 */
void fseq_parser_deinit(fseq_parser_ctx_t* ctx);

/**
 * @brief Parse and validate the 32-byte FSEQ header.
 * 
 * @param ctx Parser context.
 * @param buffer Pointer to 32 bytes of data read from the start of the file.
 * @param header_out Destination struct to fill with parsed data.
 * @return true if valid FSEQ v2 uncompressed header, false otherwise.
 */
bool fseq_parser_read_header(fseq_parser_ctx_t* ctx, const uint8_t* buffer, fseq_header_t* header_out);

/**
 * @brief Push a block of data into the parser.
 * 
 * This is the core streaming function. It consumes bytes, updates internal state,
 * and fires the pixel_cb as pixels are completed. It handles pixels that split
 * across buffer boundaries.
 * 
 * @param ctx Parser context.
 * @param data Pointer to the data chunk (e.g., read from SD card).
 * @param len Length of the chunk in bytes.
 * @return true if one or more frames were completed during this block.
 */
bool fseq_parser_push(fseq_parser_ctx_t* ctx, const uint8_t* data, uint32_t len);

/**
 * @brief Reset the parser state.
 * Call this after seeking the file pointer to the start of channel data 
 * (e.g., when looping the animation).
 */
void fseq_parser_reset(fseq_parser_ctx_t* ctx);

#endif // FSEQ_PARSER_H