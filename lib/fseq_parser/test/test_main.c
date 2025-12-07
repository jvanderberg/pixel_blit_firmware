#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "fseq_parser.h"

// Mock file data
static uint8_t mock_file_data[1024];
static uint32_t mock_file_size = 0;

// Mock read function
static uint32_t mock_read(void* user_data, uint32_t offset, void* buffer, uint32_t length) {
    (void)user_data;
    if (offset >= mock_file_size) return 0;
    
    uint32_t available = mock_file_size - offset;
    uint32_t to_read = (length < available) ? length : available;
    
    memcpy(buffer, &mock_file_data[offset], to_read);
    return to_read;
}

// Helper to create a valid header in the mock buffer
static void create_mock_fseq_v2(uint32_t channel_count, uint32_t frame_count) {
    fseq_header_t h = {0};
    h.magic = 0x51455350; // PSEQ
    h.major_version = 2;
    h.minor_version = 0;
    h.channel_data_offset = 32; // Immediately after header for simplicity
    h.header_length = 32;
    h.channel_count = channel_count;
    h.frame_count = frame_count;
    h.step_time_ms = 20; // 50fps
    h.compression_type = 0; // Uncompressed

    memcpy(mock_file_data, &h, sizeof(h));
    mock_file_size = sizeof(h) + (channel_count * frame_count);
}

void test_header_detection() {
    printf("Test: Header Detection... ");
    create_mock_fseq_v2(100, 10);
    
    fseq_parser_ctx_t* ctx = fseq_parser_init(NULL, mock_read);
    fseq_header_t header;
    
    bool success = fseq_parser_read_header(ctx, &header);
    assert(success);
    assert(header.major_version == 2);
    assert(header.channel_count == 100);
    
    fseq_parser_deinit(ctx);
    printf("PASS\n");
}

void test_invalid_magic() {
    printf("Test: Invalid Magic... ");
    create_mock_fseq_v2(100, 10);
    mock_file_data[0] = 'X'; // Break magic
    
    fseq_parser_ctx_t* ctx = fseq_parser_init(NULL, mock_read);
    fseq_header_t header;
    
    bool success = fseq_parser_read_header(ctx, &header);
    assert(!success);
    
    fseq_parser_deinit(ctx);
    printf("PASS\n");
}

void test_frame_offset_calc() {
    printf("Test: Frame Offset Calculation... ");
    create_mock_fseq_v2(150, 20); // 150 channels, 20 frames
    
    fseq_parser_ctx_t* ctx = fseq_parser_init(NULL, mock_read);
    fseq_header_t header;
    fseq_parser_read_header(ctx, &header);
    
    // Offset = DataOffset + (FrameIndex * ChannelCount)
    // Offset = 32 + (0 * 150) = 32
    assert(fseq_parser_get_frame_offset(ctx, &header, 0) == 32);
    
    // Offset = 32 + (1 * 150) = 182
    assert(fseq_parser_get_frame_offset(ctx, &header, 1) == 182);
    
    // Out of bounds
    assert(fseq_parser_get_frame_offset(ctx, &header, 20) == 0);
    
    fseq_parser_deinit(ctx);
    printf("PASS\n");
}

int main() {
    test_header_detection();
    test_invalid_magic();
    test_frame_offset_calc();
    printf("All tests passed!\n");
    return 0;
}
