#include "views.h"
#include "board_config.h"
#include <stdio.h>
#include <string.h>

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

static const char* menu_labels[MENU_COUNT] = {
    "Info",
    "Board Address",
    "SD Card",
    "String Test",
    "Toggle Test",
    "Rainbow Test",
    "String Length",
    "Brightness",
    "Shutdown",
};

static void render_main_menu(sh1106_t* display, const AppState* state) {
    sh1106_clear(display);
    sh1106_draw_string(display, 0, 0, "Pixel Blit v1.1", false);

    // Show 5 menu items with scrolling window
    #define MENU_VISIBLE 5
    int selection = state->menu_selection;

    // Calculate window start so selection is visible
    int window_start = 0;
    if (selection >= MENU_VISIBLE) {
        window_start = selection - MENU_VISIBLE + 1;
    }

    for (int i = 0; i < MENU_VISIBLE && (window_start + i) < MENU_COUNT; i++) {
        int item = window_start + i;
        bool selected = (item == selection);
        sh1106_draw_string(display, 0, 10 + i * 10, menu_labels[item], selected);
    }

    sh1106_render(display);
}

static void render_info_detail(sh1106_t* display, const AppState* state) {
    char line[24];
    sh1106_clear(display);

    // Header with string count
    snprintf(line, sizeof(line), "Strings: %u", g_board_config.string_count);
    sh1106_draw_string(display, 0, 0, line, false);

    // Calculate visible window (5 items max)
    int scroll_idx = state->info_view.scroll_index;
    int total_items = g_board_config.string_count + 1; // +1 for [Exit]

    // Keep selected item visible
    int start_idx = 0;
    if (scroll_idx > 2) start_idx = scroll_idx - 2;
    if (start_idx + 5 > total_items) start_idx = total_items - 5;
    if (start_idx < 0) start_idx = 0;

    for (int i = 0; i < 5; i++) {
        int item_idx = start_idx + i;
        if (item_idx >= total_items) break;

        bool is_selected = (item_idx == scroll_idx);
        int y = 10 + i * 9;

        if (item_idx < g_board_config.string_count) {
            // Show string config: "S00: 84px RGB"
            snprintf(line, sizeof(line), "S%02d: %3upx %s",
                     item_idx,
                     g_board_config.strings[item_idx].pixel_count,
                     color_order_name(g_board_config.strings[item_idx].color_order));
            sh1106_draw_string(display, 0, y, line, is_selected);
        } else {
            sh1106_draw_string(display, 0, y, "[ Exit ]", is_selected);
        }
    }

    sh1106_draw_string(display, 0, 56, "Nxt:scrl Sel:exit", false);
    sh1106_render(display);
}

static void render_board_address_detail(sh1106_t* display, const AppState* state) {
    char line[24];
    sh1106_clear(display);
    sh1106_draw_string(display, 0, 0, "Board Address", false);
    snprintf(line, sizeof(line), "ADC: %u", state->board_address.adc_value);
    sh1106_draw_string(display, 0, 16, line, false);
    snprintf(line, sizeof(line), "Code: 0x%X", state->board_address.code);
    sh1106_draw_string(display, 0, 24, line, false);
    snprintf(line, sizeof(line), "Err:%u M:%u", state->board_address.error, state->board_address.margin);
    sh1106_draw_string(display, 0, 32, line, false);
    sh1106_draw_string(display, 0, 48, "Next exits", false);
    sh1106_render(display);
}

static void render_sd_card_detail(sh1106_t* display, const AppState* state) {
    char line[24];
    sh1106_clear(display);

    // Show playback state
    if (state->sd_card.is_playing) {
        // Show PLAYING with AUTO indicator if enabled
        if (state->sd_card.auto_loop) {
            sh1106_draw_string(display, 0, 0, "PLAYING [AUTO]", true);
        } else {
            sh1106_draw_string(display, 0, 0, "PLAYING", true);
        }
        // Look up filename from static buffer using playing_index
        sh1106_draw_string(display, 0, 16, sd_file_list[state->sd_card.playing_index], false);
        // Show file position in playlist
        snprintf(line, sizeof(line), "File %d/%d",
                 state->sd_card.playing_index + 1, state->sd_card.file_count);
        sh1106_draw_string(display, 0, 32, line, false);
        sh1106_draw_string(display, 0, 48, "Any btn: stop", false);
        sh1106_render(display);
        return;
    }

    if (!state->sd_card.mounted) {
        sh1106_draw_string(display, 0, 0, "SD Card", false);
        sh1106_draw_string(display, 0, 16, "NOT MOUNTED", false);
        sh1106_draw_string(display, 0, 32, state->sd_card.status_msg, false);
        sh1106_draw_string(display, 0, 56, "Next: scroll", false);
        sh1106_render(display);
        return;
    }

    // Show file count
    snprintf(line, sizeof(line), "Files: %d", state->sd_card.file_count);
    sh1106_draw_string(display, 0, 0, line, false);

    // Calculate visible window (5 items max)
    int scroll_idx = state->sd_card.scroll_index;
    int total_items = state->sd_card.file_count + 1; // +1 for [Main Menu]

    // Keep selected item visible, preferably in middle
    int start_idx = 0;
    if (scroll_idx > 2) start_idx = scroll_idx - 2;
    if (start_idx + 5 > total_items) start_idx = total_items - 5;
    if (start_idx < 0) start_idx = 0;

    for (int i = 0; i < 5; i++) {
        int item_idx = start_idx + i;
        if (item_idx >= total_items) break;

        bool is_selected = (item_idx == scroll_idx);
        int y = 10 + i * 9;

        if (item_idx < state->sd_card.file_count) {
            sh1106_draw_string(display, 0, y, sd_file_list[item_idx], is_selected);
        } else {
            sh1106_draw_string(display, 0, y, "[ Main Menu ]", is_selected);
        }
    }

    sh1106_draw_string(display, 0, 56, "Sel:pick Nxt:scrl", false);
    sh1106_render(display);
}

static void render_string_test_detail(sh1106_t* display, const AppState* state) {
    sh1106_clear(display);
    sh1106_draw_string(display, 0, 0, "String Test", false);
    bool running = state->string_test.run_state == TEST_RUNNING;
    sh1106_draw_string(display, 0, 16, running ? "RUNNING" : "STOPPED", running);
    sh1106_draw_string(display, 0, 32, "Select toggles", false);
    sh1106_draw_string(display, 0, 48, "Next exits", false);
    sh1106_render(display);
}

static void render_toggle_test_detail(sh1106_t* display, const AppState* state) {
    sh1106_clear(display);
    sh1106_draw_string(display, 0, 0, "Toggle Test", false);
    bool running = state->toggle_test.run_state == TEST_RUNNING;
    sh1106_draw_string(display, 0, 16, running ? "RUNNING" : "STOPPED", running);
    sh1106_draw_string(display, 0, 32, "Select toggles", false);
    sh1106_draw_string(display, 0, 48, "Next exits", false);
    sh1106_render(display);
}

static void render_rainbow_test_detail(sh1106_t* display, const AppState* state) {
    char line[24];
    sh1106_clear(display);
    sh1106_draw_string(display, 0, 0, "Rainbow Test", false);
    snprintf(line, sizeof(line), "String: %u  FPS: %u",
             state->rainbow_test.current_string, state->rainbow_test.fps);
    sh1106_draw_string(display, 0, 16, line, false);
    bool running = state->rainbow_test.run_state == TEST_RUNNING;
    sh1106_draw_string(display, 0, 24, running ? "RUNNING" : "STOPPED", running);
    sh1106_draw_string(display, 0, 40, "Select: next str", false);
    sh1106_draw_string(display, 0, 48, "Next: exit", false);
    sh1106_render(display);
}

static void render_string_length_detail(sh1106_t* display, const AppState* state) {
    char line[24];
    sh1106_clear(display);
    sh1106_draw_string(display, 0, 0, "String Length", false);

    // Show current string and pixel position
    snprintf(line, sizeof(line), "String: %u", state->string_length.current_string);
    sh1106_draw_string(display, 0, 12, line, false);

    snprintf(line, sizeof(line), "Pixel: %u", state->string_length.current_pixel);
    sh1106_draw_string(display, 0, 22, line, true);  // Highlight current pixel

    // Show previously recorded length for this string (if any)
    uint16_t recorded = state->string_length.lengths[state->string_length.current_string];
    if (recorded > 0) {
        snprintf(line, sizeof(line), "Saved: %u", recorded);
        sh1106_draw_string(display, 0, 34, line, false);
    }

    sh1106_draw_string(display, 0, 48, "Nxt:+1 Sel:save", false);
    sh1106_draw_string(display, 0, 56, "& next string", false);
    sh1106_render(display);
}

static void render_brightness_detail(sh1106_t* display, const AppState* state) {
    char line[24];
    sh1106_clear(display);
    sh1106_draw_string(display, 0, 0, "Brightness", false);

    // Draw brightness level as number and bar
    snprintf(line, sizeof(line), "Level: %u / 10", state->brightness_level);
    sh1106_draw_string(display, 0, 16, line, false);

    // Draw visual bar [=====     ]
    char bar[14] = "[          ]";
    for (int i = 0; i < state->brightness_level; i++) {
        bar[1 + i] = '=';
    }
    sh1106_draw_string(display, 0, 28, bar, false);

    sh1106_draw_string(display, 0, 44, "Select: cycle", false);
    sh1106_draw_string(display, 0, 52, "IR +/-: adjust", false);
    sh1106_render(display);
}

void views_render(sh1106_t* display, const AppState* state) {
    // When powered off, display is blank
    if (!state->is_powered_on) {
        sh1106_clear(display);
        sh1106_render(display);
        return;
    }

    if (!state->in_detail_view) {
        render_main_menu(display, state);
        return;
    }

    switch (state->menu_selection) {
        case MENU_INFO:
            render_info_detail(display, state);
            break;
        case MENU_BOARD_ADDRESS:
            render_board_address_detail(display, state);
            break;
        case MENU_SD_CARD:
            render_sd_card_detail(display, state);
            break;
        case MENU_STRING_TEST:
            render_string_test_detail(display, state);
            break;
        case MENU_TOGGLE_TEST:
            render_toggle_test_detail(display, state);
            break;
        case MENU_RAINBOW_TEST:
            render_rainbow_test_detail(display, state);
            break;
        case MENU_STRING_LENGTH:
            render_string_length_detail(display, state);
            break;
        case MENU_BRIGHTNESS:
            render_brightness_detail(display, state);
            break;
        default:
            render_main_menu(display, state);
            break;
    }
}
