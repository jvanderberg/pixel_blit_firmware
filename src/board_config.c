#include "board_config.h"

#ifndef BOARD_CONFIG_TEST_BUILD
#include "ff.h"
#include "sd_card.h"
#include "hw_config.h"
#include "hardware/adc.h"
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Global board configuration
board_config_t g_board_config;

// ============================================================================
// Pure parsing functions (no I/O, testable on host)
// ============================================================================

pb_color_order_t board_config_parse_color_order(const char* str) {
    if (!str) return PB_COLOR_ORDER_GRB;

    // Skip leading whitespace
    while (*str == ' ' || *str == '\t') str++;

    // Compare case-insensitively
    if ((str[0] == 'R' || str[0] == 'r') &&
        (str[1] == 'G' || str[1] == 'g') &&
        (str[2] == 'B' || str[2] == 'b')) {
        return PB_COLOR_ORDER_RGB;
    }
    if ((str[0] == 'G' || str[0] == 'g') &&
        (str[1] == 'R' || str[1] == 'r') &&
        (str[2] == 'B' || str[2] == 'b')) {
        return PB_COLOR_ORDER_GRB;
    }
    if ((str[0] == 'B' || str[0] == 'b') &&
        (str[1] == 'G' || str[1] == 'g') &&
        (str[2] == 'R' || str[2] == 'r')) {
        return PB_COLOR_ORDER_BGR;
    }
    if ((str[0] == 'R' || str[0] == 'r') &&
        (str[1] == 'B' || str[1] == 'b') &&
        (str[2] == 'G' || str[2] == 'g')) {
        return PB_COLOR_ORDER_RBG;
    }
    if ((str[0] == 'G' || str[0] == 'g') &&
        (str[1] == 'B' || str[1] == 'b') &&
        (str[2] == 'R' || str[2] == 'r')) {
        return PB_COLOR_ORDER_GBR;
    }
    if ((str[0] == 'B' || str[0] == 'b') &&
        (str[1] == 'R' || str[1] == 'r') &&
        (str[2] == 'G' || str[2] == 'g')) {
        return PB_COLOR_ORDER_BRG;
    }

    // Default to GRB (most common WS2812)
    return PB_COLOR_ORDER_GRB;
}

bool board_config_parse_line(const char* line, uint16_t* pixel_count, pb_color_order_t* color_order) {
    if (!line || !pixel_count || !color_order) return false;

    // Skip empty lines and comments
    while (*line == ' ' || *line == '\t') line++;
    if (*line == '\0' || *line == '\n' || *line == '\r' || *line == '#') {
        return false;
    }

    // Check for "0" or "0," at start - treat as disabled string
    if (line[0] == '0' && (line[1] == '\0' || line[1] == '\n' || line[1] == '\r' ||
                           line[1] == ',' || line[1] == ' ' || line[1] == '\t')) {
        *pixel_count = 0;
        *color_order = PB_COLOR_ORDER_GRB;
        return true;
    }

    // Find comma separator
    const char* comma = strchr(line, ',');
    if (!comma) return false;

    // Parse pixel count (everything before comma)
    char num_buf[16];
    size_t num_len = comma - line;
    if (num_len >= sizeof(num_buf)) return false;
    memcpy(num_buf, line, num_len);
    num_buf[num_len] = '\0';

    // Validate it's a number
    for (size_t i = 0; i < num_len; i++) {
        if (num_buf[i] == ' ' || num_buf[i] == '\t') continue;
        if (num_buf[i] < '0' || num_buf[i] > '9') return false;
    }

    *pixel_count = (uint16_t)atoi(num_buf);
    *color_order = board_config_parse_color_order(comma + 1);

    return true;
}

board_config_parse_result_t board_config_parse_buffer(
    const char* buffer,
    size_t buffer_len,
    uint8_t board_id,
    board_config_t* config
) {
    board_config_parse_result_t result = {
        .success = false,
        .error_line = 0,
        .error_msg = NULL
    };

    if (!buffer || !config) {
        result.error_msg = "NULL parameter";
        return result;
    }

    // Initialize config
    config->loaded = false;
    config->board_id = board_id;
    config->string_count = 0;
    config->max_pixel_count = 0;
    for (int i = 0; i < BOARD_CONFIG_MAX_STRINGS; i++) {
        config->strings[i].pixel_count = 0;
        config->strings[i].color_order = PB_COLOR_ORDER_GRB;
    }

    // Calculate which rows we need
    uint16_t start_row = board_id * BOARD_CONFIG_MAX_STRINGS;
    uint16_t end_row = start_row + BOARD_CONFIG_MAX_STRINGS - 1;

    // Parse line by line
    uint16_t current_row = 0;
    const char* line_start = buffer;
    const char* buffer_end = buffer + buffer_len;

    while (line_start < buffer_end) {
        // Find end of line
        const char* line_end = line_start;
        while (line_end < buffer_end && *line_end != '\n' && *line_end != '\r') {
            line_end++;
        }

        // Copy line to temp buffer for parsing
        char line_buf[64];
        size_t line_len = line_end - line_start;
        if (line_len >= sizeof(line_buf)) line_len = sizeof(line_buf) - 1;
        memcpy(line_buf, line_start, line_len);
        line_buf[line_len] = '\0';

        // Check if this is a comment or empty line (skip without counting as a row)
        const char* p = line_buf;
        while (*p == ' ' || *p == '\t') p++;
        bool is_data_line = (*p != '\0' && *p != '\n' && *p != '\r' && *p != '#');

        // Move past this line (handle \r\n, \n, or \r)
        line_start = line_end;
        if (line_start < buffer_end && *line_start == '\r') line_start++;
        if (line_start < buffer_end && *line_start == '\n') line_start++;

        // Skip comments and empty lines entirely (don't count as rows)
        if (!is_data_line) {
            continue;
        }

        // Process this line if it's in our board's range
        if (current_row >= start_row && current_row <= end_row) {
            uint8_t string_index = current_row - start_row;
            uint16_t pixel_count;
            pb_color_order_t color_order;

            if (!board_config_parse_line(line_buf, &pixel_count, &color_order)) {
                result.error_line = current_row + 1;  // 1-indexed for user
                result.error_msg = "Invalid format";
                return result;
            }

            config->strings[string_index].pixel_count = pixel_count;
            config->strings[string_index].color_order = color_order;

            if (pixel_count > config->max_pixel_count) {
                config->max_pixel_count = pixel_count;
            }
            if (pixel_count > 0) {
                config->string_count = string_index + 1;
            }
        }

        current_row++;

        // Stop if we've passed our board's section
        if (current_row > end_row) break;
    }

    // Check if we found any data for this board
    if (current_row < start_row) {
        result.error_msg = "Board section not found";
        return result;
    }

    config->loaded = true;
    result.success = true;
    return result;
}

void board_config_set_defaults(uint8_t board_id) {
    g_board_config.loaded = false;
    g_board_config.board_id = board_id;
    g_board_config.string_count = BOARD_CONFIG_MAX_STRINGS;
    g_board_config.max_pixel_count = 50;

    for (int i = 0; i < BOARD_CONFIG_MAX_STRINGS; i++) {
        g_board_config.strings[i].pixel_count = 50;
        g_board_config.strings[i].color_order = PB_COLOR_ORDER_GRB;
    }
}

pb_color_order_t board_config_get_color_order(uint8_t string) {
    if (string >= BOARD_CONFIG_MAX_STRINGS) {
        return PB_COLOR_ORDER_GRB;
    }
    return g_board_config.strings[string].color_order;
}

uint16_t board_config_get_pixel_count(uint8_t string) {
    if (string >= BOARD_CONFIG_MAX_STRINGS) {
        return 0;
    }
    return g_board_config.strings[string].pixel_count;
}

// ============================================================================
// Hardware-dependent functions (only compiled for target)
// ============================================================================

#ifndef BOARD_CONFIG_TEST_BUILD

board_config_load_result_t board_config_load_from_sd(uint8_t board_id) {
    board_config_load_result_t result = {
        .success = false,
        .error_msg = "Unknown error"
    };

    // Set defaults first (fallback if load fails)
    board_config_set_defaults(board_id);

    sd_card_t *sd = sd_get_by_num(0);
    FRESULT fr = f_mount(&sd->fatfs, sd->pcName, 1);

    if (fr != FR_OK) {
        result.error_msg = "SD mount failed";
        return result;
    }

    FIL file;
    fr = f_open(&file, "/config.csv", FA_READ);
    if (fr != FR_OK) {
        result.error_msg = "config.csv not found";
        return result;
    }

    // Read entire file into buffer (max 8KB should be plenty)
    static char file_buffer[8192];
    UINT bytes_read;
    fr = f_read(&file, file_buffer, sizeof(file_buffer) - 1, &bytes_read);
    f_close(&file);

    if (fr != FR_OK) {
        result.error_msg = "Read error";
        return result;
    }
    file_buffer[bytes_read] = '\0';

    // Parse the buffer
    board_config_parse_result_t parse_result = board_config_parse_buffer(
        file_buffer, bytes_read, board_id, &g_board_config
    );

    if (!parse_result.success) {
        // Format error message with line number
        static char error_buf[48];
        if (parse_result.error_line > 0) {
            snprintf(error_buf, sizeof(error_buf), "Line %u: %s",
                     parse_result.error_line, parse_result.error_msg);
        } else {
            snprintf(error_buf, sizeof(error_buf), "%s", parse_result.error_msg);
        }
        result.error_msg = error_buf;
        return result;
    }

    result.success = true;
    result.error_msg = NULL;
    return result;
}

#endif // BOARD_CONFIG_TEST_BUILD
