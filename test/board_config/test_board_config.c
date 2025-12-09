/**
 * test_board_config.c - Unit tests for board_config parser
 *
 * Build: mkdir build_test && cd build_test && cmake .. && make
 * Run: ./test_board_config
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "board_config.h"

// ============================================================================
// Test utilities
// ============================================================================

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    static void test_##name(void); \
    static void run_test_##name(void) { \
        printf("  TEST: %s ... ", #name); \
        tests_run++; \
        test_##name(); \
        tests_passed++; \
        printf("PASS\n"); \
    } \
    static void test_##name(void)

#define RUN_TEST(name) run_test_##name()

#define ASSERT_EQ(expected, actual) \
    do { \
        if ((expected) != (actual)) { \
            printf("FAIL\n    Expected: %d, Got: %d at line %d\n", \
                   (int)(expected), (int)(actual), __LINE__); \
            exit(1); \
        } \
    } while(0)

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            printf("FAIL\n    Condition false at line %d\n", __LINE__); \
            exit(1); \
        } \
    } while(0)

#define ASSERT_FALSE(cond) \
    do { \
        if ((cond)) { \
            printf("FAIL\n    Condition should be false at line %d\n", __LINE__); \
            exit(1); \
        } \
    } while(0)

#define ASSERT_STR_EQ(expected, actual) \
    do { \
        if (strcmp((expected), (actual)) != 0) { \
            printf("FAIL\n    Expected: \"%s\", Got: \"%s\" at line %d\n", \
                   (expected), (actual), __LINE__); \
            exit(1); \
        } \
    } while(0)

// ============================================================================
// Color order parsing tests
// ============================================================================

TEST(parse_color_order_rgb) {
    ASSERT_EQ(PB_COLOR_ORDER_RGB, board_config_parse_color_order("RGB"));
    ASSERT_EQ(PB_COLOR_ORDER_RGB, board_config_parse_color_order("rgb"));
    ASSERT_EQ(PB_COLOR_ORDER_RGB, board_config_parse_color_order("Rgb"));
}

TEST(parse_color_order_grb) {
    ASSERT_EQ(PB_COLOR_ORDER_GRB, board_config_parse_color_order("GRB"));
    ASSERT_EQ(PB_COLOR_ORDER_GRB, board_config_parse_color_order("grb"));
}

TEST(parse_color_order_bgr) {
    ASSERT_EQ(PB_COLOR_ORDER_BGR, board_config_parse_color_order("BGR"));
}

TEST(parse_color_order_rbg) {
    ASSERT_EQ(PB_COLOR_ORDER_RBG, board_config_parse_color_order("RBG"));
}

TEST(parse_color_order_gbr) {
    ASSERT_EQ(PB_COLOR_ORDER_GBR, board_config_parse_color_order("GBR"));
}

TEST(parse_color_order_brg) {
    ASSERT_EQ(PB_COLOR_ORDER_BRG, board_config_parse_color_order("BRG"));
}

TEST(parse_color_order_with_whitespace) {
    ASSERT_EQ(PB_COLOR_ORDER_RGB, board_config_parse_color_order("  RGB"));
    ASSERT_EQ(PB_COLOR_ORDER_GRB, board_config_parse_color_order("\tGRB"));
}

TEST(parse_color_order_with_trailing_chars) {
    // Should match the first 3 chars
    ASSERT_EQ(PB_COLOR_ORDER_RGB, board_config_parse_color_order("RGB\n"));
    ASSERT_EQ(PB_COLOR_ORDER_RGB, board_config_parse_color_order("RGB extra"));
}

TEST(parse_color_order_invalid_defaults_grb) {
    ASSERT_EQ(PB_COLOR_ORDER_GRB, board_config_parse_color_order("XXX"));
    ASSERT_EQ(PB_COLOR_ORDER_GRB, board_config_parse_color_order(""));
    ASSERT_EQ(PB_COLOR_ORDER_GRB, board_config_parse_color_order(NULL));
}

// ============================================================================
// Line parsing tests
// ============================================================================

TEST(parse_line_valid) {
    uint16_t pixel_count;
    pb_color_order_t color_order;

    ASSERT_TRUE(board_config_parse_line("50,GRB", &pixel_count, &color_order));
    ASSERT_EQ(50, pixel_count);
    ASSERT_EQ(PB_COLOR_ORDER_GRB, color_order);
}

TEST(parse_line_different_values) {
    uint16_t pixel_count;
    pb_color_order_t color_order;

    ASSERT_TRUE(board_config_parse_line("100,RGB", &pixel_count, &color_order));
    ASSERT_EQ(100, pixel_count);
    ASSERT_EQ(PB_COLOR_ORDER_RGB, color_order);

    ASSERT_TRUE(board_config_parse_line("256,BGR", &pixel_count, &color_order));
    ASSERT_EQ(256, pixel_count);
    ASSERT_EQ(PB_COLOR_ORDER_BGR, color_order);
}

TEST(parse_line_zero_pixels) {
    uint16_t pixel_count;
    pb_color_order_t color_order;

    ASSERT_TRUE(board_config_parse_line("0,GRB", &pixel_count, &color_order));
    ASSERT_EQ(0, pixel_count);
}

TEST(parse_line_zero_without_color_order) {
    uint16_t pixel_count;
    pb_color_order_t color_order;

    // "0" alone should be treated as disabled string
    ASSERT_TRUE(board_config_parse_line("0", &pixel_count, &color_order));
    ASSERT_EQ(0, pixel_count);
    ASSERT_EQ(PB_COLOR_ORDER_GRB, color_order);

    // "0" with newline
    ASSERT_TRUE(board_config_parse_line("0\n", &pixel_count, &color_order));
    ASSERT_EQ(0, pixel_count);

    // "0" with space
    ASSERT_TRUE(board_config_parse_line("0 ", &pixel_count, &color_order));
    ASSERT_EQ(0, pixel_count);
}

TEST(parse_line_with_whitespace) {
    uint16_t pixel_count;
    pb_color_order_t color_order;

    ASSERT_TRUE(board_config_parse_line("  50,GRB", &pixel_count, &color_order));
    ASSERT_EQ(50, pixel_count);
}

TEST(parse_line_empty) {
    uint16_t pixel_count;
    pb_color_order_t color_order;

    ASSERT_FALSE(board_config_parse_line("", &pixel_count, &color_order));
    ASSERT_FALSE(board_config_parse_line("   ", &pixel_count, &color_order));
    ASSERT_FALSE(board_config_parse_line("\n", &pixel_count, &color_order));
}

TEST(parse_line_comment) {
    uint16_t pixel_count;
    pb_color_order_t color_order;

    ASSERT_FALSE(board_config_parse_line("# comment", &pixel_count, &color_order));
    ASSERT_FALSE(board_config_parse_line("  # comment", &pixel_count, &color_order));
}

TEST(parse_line_no_comma) {
    uint16_t pixel_count;
    pb_color_order_t color_order;

    ASSERT_FALSE(board_config_parse_line("50GRB", &pixel_count, &color_order));
    ASSERT_FALSE(board_config_parse_line("50", &pixel_count, &color_order));
}

TEST(parse_line_invalid_number) {
    uint16_t pixel_count;
    pb_color_order_t color_order;

    ASSERT_FALSE(board_config_parse_line("abc,GRB", &pixel_count, &color_order));
    ASSERT_FALSE(board_config_parse_line("-50,GRB", &pixel_count, &color_order));
}

TEST(parse_line_null_params) {
    uint16_t pixel_count;
    pb_color_order_t color_order;

    ASSERT_FALSE(board_config_parse_line(NULL, &pixel_count, &color_order));
    ASSERT_FALSE(board_config_parse_line("50,GRB", NULL, &color_order));
    ASSERT_FALSE(board_config_parse_line("50,GRB", &pixel_count, NULL));
}

// ============================================================================
// Buffer parsing tests - single board
// ============================================================================

TEST(parse_buffer_single_board_few_strings) {
    const char* csv =
        "50,GRB\n"
        "50,GRB\n"
        "100,RGB\n";

    board_config_t config;
    board_config_parse_result_t result = board_config_parse_buffer(csv, strlen(csv), 0, &config);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(0, config.board_id);
    ASSERT_EQ(3, config.string_count);
    ASSERT_EQ(100, config.max_pixel_count);
    ASSERT_EQ(50, config.strings[0].pixel_count);
    ASSERT_EQ(PB_COLOR_ORDER_GRB, config.strings[0].color_order);
    ASSERT_EQ(50, config.strings[1].pixel_count);
    ASSERT_EQ(100, config.strings[2].pixel_count);
    ASSERT_EQ(PB_COLOR_ORDER_RGB, config.strings[2].color_order);
    // Remaining strings should be 0
    ASSERT_EQ(0, config.strings[3].pixel_count);
}

TEST(parse_buffer_single_board_with_gaps) {
    // String 0 and 2 configured, string 1 disabled (0 pixels)
    const char* csv =
        "50,GRB\n"
        "0,GRB\n"
        "100,RGB\n";

    board_config_t config;
    board_config_parse_result_t result = board_config_parse_buffer(csv, strlen(csv), 0, &config);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(3, config.string_count);  // Highest string index + 1
    ASSERT_EQ(100, config.max_pixel_count);
    ASSERT_EQ(50, config.strings[0].pixel_count);
    ASSERT_EQ(0, config.strings[1].pixel_count);  // Gap
    ASSERT_EQ(100, config.strings[2].pixel_count);
}

TEST(parse_buffer_full_32_strings) {
    // Generate 32 lines
    char csv[2048];
    char* p = csv;
    for (int i = 0; i < 32; i++) {
        p += sprintf(p, "%d,GRB\n", (i + 1) * 10);
    }

    board_config_t config;
    board_config_parse_result_t result = board_config_parse_buffer(csv, strlen(csv), 0, &config);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(32, config.string_count);
    ASSERT_EQ(320, config.max_pixel_count);  // 32 * 10
    ASSERT_EQ(10, config.strings[0].pixel_count);
    ASSERT_EQ(320, config.strings[31].pixel_count);
}

TEST(parse_buffer_with_comments_and_empty_lines) {
    const char* csv =
        "# Header comment\n"
        "50,GRB\n"
        "\n"
        "# Another comment\n"
        "100,RGB\n";

    board_config_t config;
    board_config_parse_result_t result = board_config_parse_buffer(csv, strlen(csv), 0, &config);

    ASSERT_TRUE(result.success);
    // Comments and empty lines are completely ignored (don't count as rows)
    // Data line 0: 50,GRB -> string 0
    // Data line 1: 100,RGB -> string 1
    ASSERT_EQ(50, config.strings[0].pixel_count);
    ASSERT_EQ(PB_COLOR_ORDER_GRB, config.strings[0].color_order);
    ASSERT_EQ(100, config.strings[1].pixel_count);
    ASSERT_EQ(PB_COLOR_ORDER_RGB, config.strings[1].color_order);
    ASSERT_EQ(2, config.string_count);
}

TEST(parse_buffer_crlf_line_endings) {
    const char* csv = "50,GRB\r\n100,RGB\r\n";

    board_config_t config;
    board_config_parse_result_t result = board_config_parse_buffer(csv, strlen(csv), 0, &config);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(50, config.strings[0].pixel_count);
    ASSERT_EQ(100, config.strings[1].pixel_count);
}

TEST(parse_buffer_cr_only_line_endings) {
    const char* csv = "50,GRB\r100,RGB\r";

    board_config_t config;
    board_config_parse_result_t result = board_config_parse_buffer(csv, strlen(csv), 0, &config);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(50, config.strings[0].pixel_count);
    ASSERT_EQ(100, config.strings[1].pixel_count);
}

// ============================================================================
// Buffer parsing tests - multiple boards
// ============================================================================

TEST(parse_buffer_board_1_few_strings) {
    // Board 0: 32 rows, Board 1 starts at row 32
    char csv[4096];
    char* p = csv;

    // Board 0: 32 rows of 10 pixels
    for (int i = 0; i < 32; i++) {
        p += sprintf(p, "10,GRB\n");
    }
    // Board 1: 5 rows of different values
    p += sprintf(p, "50,RGB\n");
    p += sprintf(p, "60,BGR\n");
    p += sprintf(p, "70,GRB\n");
    p += sprintf(p, "80,RBG\n");
    p += sprintf(p, "90,GBR\n");

    board_config_t config;
    board_config_parse_result_t result = board_config_parse_buffer(csv, strlen(csv), 1, &config);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(1, config.board_id);
    ASSERT_EQ(5, config.string_count);
    ASSERT_EQ(90, config.max_pixel_count);
    ASSERT_EQ(50, config.strings[0].pixel_count);
    ASSERT_EQ(PB_COLOR_ORDER_RGB, config.strings[0].color_order);
    ASSERT_EQ(60, config.strings[1].pixel_count);
    ASSERT_EQ(PB_COLOR_ORDER_BGR, config.strings[1].color_order);
    ASSERT_EQ(90, config.strings[4].pixel_count);
    ASSERT_EQ(PB_COLOR_ORDER_GBR, config.strings[4].color_order);
}

TEST(parse_buffer_board_2_full_32_strings) {
    char csv[8192];
    char* p = csv;

    // Board 0: 32 rows
    for (int i = 0; i < 32; i++) {
        p += sprintf(p, "10,GRB\n");
    }
    // Board 1: 32 rows
    for (int i = 0; i < 32; i++) {
        p += sprintf(p, "20,GRB\n");
    }
    // Board 2: 32 rows with varying config
    for (int i = 0; i < 32; i++) {
        p += sprintf(p, "%d,%s\n", (i + 1) * 5, (i % 2 == 0) ? "RGB" : "GRB");
    }

    board_config_t config;
    board_config_parse_result_t result = board_config_parse_buffer(csv, strlen(csv), 2, &config);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(2, config.board_id);
    ASSERT_EQ(32, config.string_count);
    ASSERT_EQ(160, config.max_pixel_count);  // 32 * 5
    ASSERT_EQ(5, config.strings[0].pixel_count);
    ASSERT_EQ(PB_COLOR_ORDER_RGB, config.strings[0].color_order);
    ASSERT_EQ(10, config.strings[1].pixel_count);
    ASSERT_EQ(PB_COLOR_ORDER_GRB, config.strings[1].color_order);
}

TEST(parse_buffer_board_with_zero_in_middle) {
    char csv[4096];
    char* p = csv;

    // Board 0: 32 rows
    for (int i = 0; i < 32; i++) {
        p += sprintf(p, "10,GRB\n");
    }
    // Board 1: strings 0-2 active, 3-5 disabled, 6-7 active
    p += sprintf(p, "50,GRB\n");  // string 0
    p += sprintf(p, "50,GRB\n");  // string 1
    p += sprintf(p, "50,GRB\n");  // string 2
    p += sprintf(p, "0,GRB\n");   // string 3 disabled
    p += sprintf(p, "0,GRB\n");   // string 4 disabled
    p += sprintf(p, "0,GRB\n");   // string 5 disabled
    p += sprintf(p, "100,RGB\n"); // string 6
    p += sprintf(p, "100,RGB\n"); // string 7

    board_config_t config;
    board_config_parse_result_t result = board_config_parse_buffer(csv, strlen(csv), 1, &config);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(8, config.string_count);  // Highest active + 1
    ASSERT_EQ(100, config.max_pixel_count);
    ASSERT_EQ(50, config.strings[0].pixel_count);
    ASSERT_EQ(50, config.strings[2].pixel_count);
    ASSERT_EQ(0, config.strings[3].pixel_count);
    ASSERT_EQ(0, config.strings[4].pixel_count);
    ASSERT_EQ(0, config.strings[5].pixel_count);
    ASSERT_EQ(100, config.strings[6].pixel_count);
    ASSERT_EQ(100, config.strings[7].pixel_count);
}

// ============================================================================
// Error handling tests
// ============================================================================

TEST(parse_buffer_malformed_line) {
    const char* csv =
        "50,GRB\n"
        "not_a_number,RGB\n"
        "100,BGR\n";

    board_config_t config;
    board_config_parse_result_t result = board_config_parse_buffer(csv, strlen(csv), 0, &config);

    ASSERT_FALSE(result.success);
    ASSERT_EQ(2, result.error_line);  // 1-indexed
    ASSERT_TRUE(result.error_msg != NULL);
}

TEST(parse_buffer_missing_comma) {
    const char* csv =
        "50,GRB\n"
        "100 RGB\n";  // Missing comma

    board_config_t config;
    board_config_parse_result_t result = board_config_parse_buffer(csv, strlen(csv), 0, &config);

    ASSERT_FALSE(result.success);
    ASSERT_EQ(2, result.error_line);
}

TEST(parse_buffer_negative_number) {
    const char* csv = "-50,GRB\n";

    board_config_t config;
    board_config_parse_result_t result = board_config_parse_buffer(csv, strlen(csv), 0, &config);

    ASSERT_FALSE(result.success);
    ASSERT_EQ(1, result.error_line);
}

TEST(parse_buffer_board_not_found) {
    // Only has data for board 0
    const char* csv = "50,GRB\n";

    board_config_t config;
    board_config_parse_result_t result = board_config_parse_buffer(csv, strlen(csv), 5, &config);

    ASSERT_FALSE(result.success);
    ASSERT_TRUE(result.error_msg != NULL);
}

TEST(parse_buffer_null_params) {
    const char* csv = "50,GRB\n";
    board_config_t config;

    board_config_parse_result_t result = board_config_parse_buffer(NULL, 10, 0, &config);
    ASSERT_FALSE(result.success);

    result = board_config_parse_buffer(csv, strlen(csv), 0, NULL);
    ASSERT_FALSE(result.success);
}

TEST(parse_buffer_empty) {
    const char* csv = "";
    board_config_t config;

    board_config_parse_result_t result = board_config_parse_buffer(csv, 0, 0, &config);

    // Empty file for board 0 should succeed with 0 strings
    ASSERT_TRUE(result.success);
    ASSERT_EQ(0, config.string_count);
}

TEST(parse_buffer_all_comments) {
    const char* csv =
        "# comment 1\n"
        "# comment 2\n"
        "# comment 3\n";

    board_config_t config;
    board_config_parse_result_t result = board_config_parse_buffer(csv, strlen(csv), 0, &config);

    // All comments should succeed with 0 active strings
    ASSERT_TRUE(result.success);
    ASSERT_EQ(0, config.string_count);
}

// ============================================================================
// Sample file content tests (match test/sample_configs/*.csv)
// ============================================================================

TEST(sample_config_single_board_3_strings) {
    // Matches test/sample_configs/config_single_board_3_strings.csv
    const char* csv =
        "50,GRB\n"
        "50,GRB\n"
        "100,RGB\n";

    board_config_t config;
    board_config_parse_result_t result = board_config_parse_buffer(csv, strlen(csv), 0, &config);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(3, config.string_count);
    ASSERT_EQ(100, config.max_pixel_count);
    ASSERT_EQ(50, config.strings[0].pixel_count);
    ASSERT_EQ(PB_COLOR_ORDER_GRB, config.strings[0].color_order);
    ASSERT_EQ(50, config.strings[1].pixel_count);
    ASSERT_EQ(PB_COLOR_ORDER_GRB, config.strings[1].color_order);
    ASSERT_EQ(100, config.strings[2].pixel_count);
    ASSERT_EQ(PB_COLOR_ORDER_RGB, config.strings[2].color_order);
}

TEST(sample_config_single_board_gaps) {
    // Matches test/sample_configs/config_single_board_gaps.csv
    const char* csv =
        "50,GRB\n"
        "50,GRB\n"
        "50,GRB\n"
        "0,GRB\n"
        "0,GRB\n"
        "0,GRB\n"
        "100,RGB\n"
        "100,RGB\n";

    board_config_t config;
    board_config_parse_result_t result = board_config_parse_buffer(csv, strlen(csv), 0, &config);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(8, config.string_count);
    ASSERT_EQ(100, config.max_pixel_count);
    ASSERT_EQ(50, config.strings[0].pixel_count);
    ASSERT_EQ(50, config.strings[1].pixel_count);
    ASSERT_EQ(50, config.strings[2].pixel_count);
    ASSERT_EQ(0, config.strings[3].pixel_count);
    ASSERT_EQ(0, config.strings[4].pixel_count);
    ASSERT_EQ(0, config.strings[5].pixel_count);
    ASSERT_EQ(100, config.strings[6].pixel_count);
    ASSERT_EQ(PB_COLOR_ORDER_RGB, config.strings[6].color_order);
    ASSERT_EQ(100, config.strings[7].pixel_count);
}

TEST(sample_config_two_boards_board0) {
    // Matches test/sample_configs/config_two_boards.csv, board 0
    const char* csv =
        "50,GRB\n50,GRB\n50,GRB\n50,GRB\n"
        "0,GRB\n0,GRB\n0,GRB\n0,GRB\n"
        "0,GRB\n0,GRB\n0,GRB\n0,GRB\n"
        "0,GRB\n0,GRB\n0,GRB\n0,GRB\n"
        "0,GRB\n0,GRB\n0,GRB\n0,GRB\n"
        "0,GRB\n0,GRB\n0,GRB\n0,GRB\n"
        "0,GRB\n0,GRB\n0,GRB\n0,GRB\n"
        "0,GRB\n0,GRB\n0,GRB\n0,GRB\n"
        "100,RGB\n100,RGB\n100,RGB\n";

    board_config_t config;
    board_config_parse_result_t result = board_config_parse_buffer(csv, strlen(csv), 0, &config);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(0, config.board_id);
    ASSERT_EQ(4, config.string_count);
    ASSERT_EQ(50, config.max_pixel_count);
    ASSERT_EQ(50, config.strings[0].pixel_count);
    ASSERT_EQ(50, config.strings[3].pixel_count);
    ASSERT_EQ(0, config.strings[4].pixel_count);
}

TEST(sample_config_two_boards_board1) {
    // Matches test/sample_configs/config_two_boards.csv, board 1
    const char* csv =
        "50,GRB\n50,GRB\n50,GRB\n50,GRB\n"
        "0,GRB\n0,GRB\n0,GRB\n0,GRB\n"
        "0,GRB\n0,GRB\n0,GRB\n0,GRB\n"
        "0,GRB\n0,GRB\n0,GRB\n0,GRB\n"
        "0,GRB\n0,GRB\n0,GRB\n0,GRB\n"
        "0,GRB\n0,GRB\n0,GRB\n0,GRB\n"
        "0,GRB\n0,GRB\n0,GRB\n0,GRB\n"
        "0,GRB\n0,GRB\n0,GRB\n0,GRB\n"
        "100,RGB\n100,RGB\n100,RGB\n";

    board_config_t config;
    board_config_parse_result_t result = board_config_parse_buffer(csv, strlen(csv), 1, &config);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(1, config.board_id);
    ASSERT_EQ(3, config.string_count);
    ASSERT_EQ(100, config.max_pixel_count);
    ASSERT_EQ(100, config.strings[0].pixel_count);
    ASSERT_EQ(PB_COLOR_ORDER_RGB, config.strings[0].color_order);
    ASSERT_EQ(100, config.strings[1].pixel_count);
    ASSERT_EQ(100, config.strings[2].pixel_count);
    ASSERT_EQ(0, config.strings[3].pixel_count);
}

TEST(sample_config_malformed) {
    // Matches test/sample_configs/config_malformed.csv
    const char* csv =
        "50,GRB\n"
        "not_a_number,RGB\n"
        "100,BGR\n";

    board_config_t config;
    board_config_parse_result_t result = board_config_parse_buffer(csv, strlen(csv), 0, &config);

    ASSERT_FALSE(result.success);
    ASSERT_EQ(2, result.error_line);  // Line 2 is malformed
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    printf("\n=== Board Config Parser Tests ===\n\n");

    printf("Color order parsing:\n");
    RUN_TEST(parse_color_order_rgb);
    RUN_TEST(parse_color_order_grb);
    RUN_TEST(parse_color_order_bgr);
    RUN_TEST(parse_color_order_rbg);
    RUN_TEST(parse_color_order_gbr);
    RUN_TEST(parse_color_order_brg);
    RUN_TEST(parse_color_order_with_whitespace);
    RUN_TEST(parse_color_order_with_trailing_chars);
    RUN_TEST(parse_color_order_invalid_defaults_grb);

    printf("\nLine parsing:\n");
    RUN_TEST(parse_line_valid);
    RUN_TEST(parse_line_different_values);
    RUN_TEST(parse_line_zero_pixels);
    RUN_TEST(parse_line_zero_without_color_order);
    RUN_TEST(parse_line_with_whitespace);
    RUN_TEST(parse_line_empty);
    RUN_TEST(parse_line_comment);
    RUN_TEST(parse_line_no_comma);
    RUN_TEST(parse_line_invalid_number);
    RUN_TEST(parse_line_null_params);

    printf("\nSingle board parsing:\n");
    RUN_TEST(parse_buffer_single_board_few_strings);
    RUN_TEST(parse_buffer_single_board_with_gaps);
    RUN_TEST(parse_buffer_full_32_strings);
    RUN_TEST(parse_buffer_with_comments_and_empty_lines);
    RUN_TEST(parse_buffer_crlf_line_endings);
    RUN_TEST(parse_buffer_cr_only_line_endings);

    printf("\nMultiple board parsing:\n");
    RUN_TEST(parse_buffer_board_1_few_strings);
    RUN_TEST(parse_buffer_board_2_full_32_strings);
    RUN_TEST(parse_buffer_board_with_zero_in_middle);

    printf("\nError handling:\n");
    RUN_TEST(parse_buffer_malformed_line);
    RUN_TEST(parse_buffer_missing_comma);
    RUN_TEST(parse_buffer_negative_number);
    RUN_TEST(parse_buffer_board_not_found);
    RUN_TEST(parse_buffer_null_params);
    RUN_TEST(parse_buffer_empty);
    RUN_TEST(parse_buffer_all_comments);

    printf("\nSample config files (test/sample_configs/):\n");
    RUN_TEST(sample_config_single_board_3_strings);
    RUN_TEST(sample_config_single_board_gaps);
    RUN_TEST(sample_config_two_boards_board0);
    RUN_TEST(sample_config_two_boards_board1);
    RUN_TEST(sample_config_malformed);

    printf("\n=================================\n");
    printf("Tests: %d passed / %d total\n", tests_passed, tests_run);
    printf("=================================\n\n");

    return (tests_passed == tests_run) ? 0 : 1;
}
