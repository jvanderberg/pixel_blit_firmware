#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "fseq_parser.h"

// Mock tracking for callback
typedef struct {
    int count;
    uint8_t last_string;
    uint16_t last_pixel;
    uint32_t last_color;
} mock_cb_data_t;

void mock_pixel_cb(void* user_data, uint8_t string, uint16_t pixel, uint32_t color) {
    mock_cb_data_t* d = (mock_cb_data_t*)user_data;
    d->count++;
    d->last_string = string;
    d->last_pixel = pixel;
    d->last_color = color;
    
    // Debug output
    // printf("  -> String %d, Pixel %d, Color 0x%06X\n", string, pixel, color);
}

void test_streaming_parsing() {
    printf("Test: Streaming Parsing (Straddled Boundaries)... ");
    
    // Setup Layout: 2 Strings, 2 Pixels each = 4 Pixels total = 12 Channels
    uint16_t lengths[] = { 2, 2 };
    fseq_layout_t layout = { .num_strings = 2, .string_lengths = lengths };
    mock_cb_data_t cb_data = {0};
    
    fseq_parser_ctx_t* ctx = fseq_parser_init(&cb_data, mock_pixel_cb, layout);
    
    // Setup Header (12 channels)
    fseq_header_t h = {0};
    h.magic = 0x51455350;
    h.major_version = 2;
    h.channel_count = 12; 
    fseq_parser_read_header(ctx, (uint8_t*)&h, &h);
    
    // Frame Data: 12 bytes (RGB RGB RGB RGB)
    // S0P0: 0x010203, S0P1: 0x040506
    // S1P0: 0x070809, S1P1: 0x0A0B0C
    uint8_t frame[] = {
        1, 2, 3, 
        4, 5, 6, 
        7, 8, 9, 
        10, 11, 12
    };
    
    // Push in chunks to simulate uneven SD reads
    // Chunk 1: 4 bytes (S0P0 complete, S0P1 partial R)
    bool done = fseq_parser_push(ctx, frame, 4);
    assert(!done);
    assert(cb_data.count == 1); // S0P0
    assert(cb_data.last_color == 0x010203);
    
    // Chunk 2: 2 bytes (S0P1 G, B)
    done = fseq_parser_push(ctx, frame + 4, 2);
    assert(!done);
    assert(cb_data.count == 2); // S0P1
    assert(cb_data.last_color == 0x040506);
    assert(cb_data.last_string == 0 && cb_data.last_pixel == 1);
    
    // Chunk 3: Remaining 6 bytes
    done = fseq_parser_push(ctx, frame + 6, 6);
    assert(done); // Frame complete!
    assert(cb_data.count == 4); // All 4 pixels
    assert(cb_data.last_color == 0x0A0B0C);
    assert(cb_data.last_string == 1 && cb_data.last_pixel == 1);
    
    fseq_parser_deinit(ctx);
    printf("PASS\n");
}

void test_variable_length_strings() {
    printf("Test: Variable Length Strings... ");
    
    // Setup Layout: 
    // String 0: 2 pixels (Channels 0-5)
    // String 1: 1 pixel  (Channels 6-8)
    // Total: 3 pixels, 9 channels
    uint16_t lengths[] = { 2, 1 };
    fseq_layout_t layout = {
        .num_strings = 2,
        .string_lengths = lengths
    };
    
    mock_cb_data_t cb_data = {0};
    fseq_parser_ctx_t* ctx = fseq_parser_init(&cb_data, mock_pixel_cb, layout);
    
    // Header
    fseq_header_t h = {0};
    h.magic = 0x51455350;
    h.major_version = 2;
    h.channel_count = 9;
    fseq_parser_read_header(ctx, (uint8_t*)&h, &h);
    
    // Data: 9 bytes
    // S0P0 (0xAAAAAA), S0P1 (0xBBBBBB), S1P0 (0xCCCCCC)
    uint8_t data[] = {
        0xAA, 0xAA, 0xAA,
        0xBB, 0xBB, 0xBB,
        0xCC, 0xCC, 0xCC
    };
    
    fseq_parser_push(ctx, data, 9);
    
    assert(cb_data.count == 3);
    
    // We can't verify exact sequence with just "last_color" in the mock,
    // but we can trust the logic if we added logging or a stricter mock.
    // For now, verify the last pixel was S1, P0
    assert(cb_data.last_string == 1);
    assert(cb_data.last_pixel == 0);
    assert(cb_data.last_color == 0xCCCCCC);
    
    fseq_parser_deinit(ctx);
    printf("PASS\n");
}

int main() {
    test_streaming_parsing();
    test_variable_length_strings();
    printf("All streaming tests passed!\n");
    return 0;
}
