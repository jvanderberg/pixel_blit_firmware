#include <stdio.h>
#include <stdbool.h>
#include <limits.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/multicore.h"
#include "pico/flash.h"  // For flash_safe_execute_core_init
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"  // For __dmb() memory barrier

// SD Card / FatFS
#include "ff.h"
#include "sd_card.h"
#include "hw_config.h"

// FSEQ Player
#include "fseq_player.h"

// String Length Test
#include "string_length_test.h"

// IR Remote Control
#include "ir_control.h"

// SD Card Operations
#include "sd_ops.h"

// Board configuration
#include "board_config.h"

#include "app_state.h"
#include "action.h"
#include "reducer.h"
#include "side_effects.h"
#include "views.h"
#include "flash_settings.h"
#include "pb_led_driver.h"

// Pin definitions
#define DISP_SDA_PIN 46
#define DISP_SCL_PIN 47
#define OLED_I2C i2c1
#define OLED_ADDR 0x3C

#define BTN_SELECT_PIN 43
#define BTN_NEXT_PIN 45

#define BOARD_ADDR_ADC_GPIO 40
#define BOARD_ADDR_ADC_CH 0
#define BOARD_ADDR_SAMPLES 100

#define STRING_OUT_BASE_PIN 0

// Timing
#define BTN_DEBOUNCE_US 200000  // 200ms debounce
#define TICK_1S_US 1000000      // 1 second
#define DISPLAY_REFRESH_US 500000  // 500ms display refresh for FPS

// Global state
static AppState current_state;
static HardwareContext hw_context;
static sh1106_t display;
static string_test_t string_test_ctx;
static toggle_test_t toggle_test_ctx;
static rainbow_test_t rainbow_test_ctx;
static string_length_test_t string_length_test_ctx;
static fseq_player_t fseq_player_ctx;

// Static file list buffer (declared extern in app_state.h)
char sd_file_list[SD_MAX_FILES][SD_FILENAME_LEN];

// Core1 rainbow test control (extern'd by side_effects.c)
volatile bool rainbow_core1_running = false;

void core1_rainbow_entry(void) {
    // Allow core0 to pause us for flash operations
    flash_safe_execute_core_init();

    while (true) {
        __dmb();  // Ensure we see latest flag value from Core0
        if (!rainbow_core1_running) break;
        rainbow_test_task(&rainbow_test_ctx);
    }
}

// Core1 FSEQ playback control (extern'd by side_effects.c)
volatile bool fseq_core1_running = false;

void core1_fseq_entry(void) {
    // Allow core0 to pause us for flash operations
    flash_safe_execute_core_init();

    fseq_player_core1_entry();
}

// Button state
static volatile uint64_t select_last_press_us = 0;
static volatile uint64_t next_last_press_us = 0;
static volatile bool select_pressed = false;
static volatile bool next_pressed = false;

// Board address decoding
// Theoretical ADC values for each code (sorted high→low by voltage)
// Code order: 0,8,4,C,2,A,6,E,1,9,5,D,3,B,7,F
static const uint16_t level_codes[16] = {
    4095, 3723, 3374, 3117,  // 0, 8, 4, C
    2786, 2608, 2432, 2296,  // 2, A, 6, E
    2048, 1950, 1850, 1770,  // 1, 9, 5, D
    1658, 1593, 1526, 1471   // 3, B, 7, F
};

static const uint8_t code_by_rank[16] = {
    0x0, 0x8, 0x4, 0xC,
    0x2, 0xA, 0x6, 0xE,
    0x1, 0x9, 0x5, 0xD,
    0x3, 0xB, 0x7, 0xF
};

static inline uint16_t absdiff_u16(uint16_t a, uint16_t b) {
    return (a > b) ? (uint16_t)(a - b) : (uint16_t)(b - a);
}

static void decode_board_address(uint16_t sample, uint8_t* code, uint16_t* error, uint16_t* margin) {
    uint16_t best_err = absdiff_u16(level_codes[0], sample);
    uint16_t next_err = UINT16_MAX;
    int best_rank = 0;

    for (int i = 1; i < 16; i++) {
        uint16_t err = absdiff_u16(level_codes[i], sample);
        if (err < best_err) {
            next_err = best_err;
            best_err = err;
            best_rank = i;
        } else if (err < next_err) {
            next_err = err;
        }
    }

    *code = code_by_rank[best_rank];
    *error = best_err;
    *margin = (next_err == UINT16_MAX) ? 0 : (uint16_t)(next_err - best_err);
}

static uint16_t sample_board_address_adc(void) {
    uint32_t acc = 0;
    adc_select_input(BOARD_ADDR_ADC_CH);
    for (int i = 0; i < BOARD_ADDR_SAMPLES; i++) {
        acc += adc_read();
        sleep_us(100);
    }
    return (uint16_t)(acc / BOARD_ADDR_SAMPLES);
}

// Combined GPIO ISR for buttons and IR
// Placed in RAM to avoid flash XIP latency
static void __not_in_flash_func(gpio_isr)(uint gpio, uint32_t events) {
    uint64_t now = time_us_64();

    if (gpio == BTN_SELECT_PIN && (events & GPIO_IRQ_EDGE_FALL)) {
        if (now - select_last_press_us >= BTN_DEBOUNCE_US) {
            select_pressed = true;
            select_last_press_us = now;
        }
    } else if (gpio == BTN_NEXT_PIN && (events & GPIO_IRQ_EDGE_FALL)) {
        if (now - next_last_press_us >= BTN_DEBOUNCE_US) {
            next_pressed = true;
            next_last_press_us = now;
        }
    } else if (gpio == IR_PIN && (events & GPIO_IRQ_EDGE_FALL)) {
        ir_process_edge();
    }
}

// Dispatch an action through the reducer
static void dispatch(Action action) {
    AppState old_state = current_state;

    // Run through pure reducer
    current_state = reduce(&current_state, &action);

    // Apply side effects only if state changed
    if (app_state_dirty(&old_state, &current_state)) {
        side_effects_apply(&hw_context, &old_state, &current_state);
    }
}

int main() {
    stdio_init_all();
    sleep_ms(2000);
    printf("Pixel_Blit starting (reactive architecture)...\n");

    // Initialize I2C for display
    i2c_init(OLED_I2C, 400 * 1000);
    gpio_set_function(DISP_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(DISP_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(DISP_SDA_PIN);
    gpio_pull_up(DISP_SCL_PIN);

    // Initialize display
    bool display_ready = sh1106_init(&display, OLED_I2C, OLED_ADDR);
    if (display_ready) {
        printf("OLED initialized at 0x%02X\n", OLED_ADDR);
    } else {
        printf("Failed to init OLED at 0x%02X\n", OLED_ADDR);
    }

    // Initialize buttons
    gpio_init(BTN_SELECT_PIN);
    gpio_set_function(BTN_SELECT_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(BTN_SELECT_PIN, GPIO_IN);
    gpio_pull_up(BTN_SELECT_PIN);

    gpio_init(BTN_NEXT_PIN);
    gpio_set_function(BTN_NEXT_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(BTN_NEXT_PIN, GPIO_IN);
    gpio_pull_up(BTN_NEXT_PIN);

    // Initialize ADC for board address
    adc_init();
    adc_gpio_init(BOARD_ADDR_ADC_GPIO);

    // Read board ID and load configuration from SD card
    // Delay for ADC settling after init
    sleep_ms(100);
    uint16_t adc_sample = sample_board_address_adc();
    uint8_t board_id;
    uint16_t adc_error, adc_margin;
    decode_board_address(adc_sample, &board_id, &adc_error, &adc_margin);
    printf("Board ID: %u (ADC: %u, err: %u, margin: %u)\n",
           board_id, adc_sample, adc_error, adc_margin);

    // Configure SD card library to use DMA_IRQ_1 BEFORE first SD access
    // (IRQ_0 is used by pb_led_driver)
    set_spi_dma_irq_channel(true, true);  // useChannel1=true, shared=true

    board_config_load_result_t config_result = board_config_load_from_sd(board_id);
    if (!config_result.success) {
        printf("Config: %s - using defaults\n", config_result.error_msg);

        // Show error on display and wait for button
        if (display_ready) {
            char line[24];
            sh1106_clear(&display);
            sh1106_draw_string(&display, 0, 0, "Config Error", false);
            sh1106_draw_string(&display, 0, 16, config_result.error_msg, false);
            snprintf(line, sizeof(line), "Board ID: %u", board_id);
            sh1106_draw_string(&display, 0, 32, line, false);
            sh1106_draw_string(&display, 0, 48, "Using defaults", false);
            sh1106_draw_string(&display, 0, 56, "Press button...", false);
            sh1106_render(&display);

            // Wait for button press
            while (gpio_get(BTN_SELECT_PIN) && gpio_get(BTN_NEXT_PIN)) {
                tight_loop_contents();
            }
            // Debounce
            sleep_ms(200);
        }
    } else {
        printf("Config: Loaded %u strings, max %u pixels\n",
               g_board_config.string_count, g_board_config.max_pixel_count);
    }

    // Initialize test modules
    if (!string_test_init(&string_test_ctx, STRING_OUT_BASE_PIN)) {
        printf("String test init failed\n");
    }
    if (!toggle_test_init(&toggle_test_ctx, STRING_OUT_BASE_PIN)) {
        printf("Toggle test init failed\n");
    }
    if (!rainbow_test_init(&rainbow_test_ctx, STRING_OUT_BASE_PIN)) {
        printf("Rainbow test init failed\n");
    }
    if (!string_length_test_init(&string_length_test_ctx, STRING_OUT_BASE_PIN)) {
        printf("String length test init failed\n");
    }
    if (!fseq_player_init(&fseq_player_ctx, STRING_OUT_BASE_PIN)) {
        printf("FSEQ player init failed\n");
    }

    // Setup hardware context
    hw_context.display = &display;
    hw_context.string_test = &string_test_ctx;
    hw_context.toggle_test = &toggle_test_ctx;
    hw_context.rainbow_test = &rainbow_test_ctx;
    hw_context.string_length_test = &string_length_test_ctx;
    hw_context.fseq_player = &fseq_player_ctx;

    // Load saved settings and initialize application state
    flash_settings_t saved_settings;
    if (flash_settings_load(&saved_settings)) {
        current_state = app_state_init_with_settings(
            saved_settings.brightness,
            saved_settings.was_playing,
            saved_settings.playing_index);
    } else {
        current_state = app_state_init();
    }

    // Initialize IR receiver
    ir_init(IR_PIN);
    printf("IR receiver initialized on GPIO %d\n", IR_PIN);

    // Setup GPIO interrupts (combined callback for buttons and IR)
    gpio_set_irq_enabled_with_callback(BTN_SELECT_PIN, GPIO_IRQ_EDGE_FALL, true, gpio_isr);
    gpio_set_irq_enabled(BTN_NEXT_PIN, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(IR_PIN, GPIO_IRQ_EDGE_FALL, true);

    // Give GPIO interrupts highest priority for reliable IR reception
    irq_set_priority(IO_IRQ_BANK0, 0x00);

    // Set initial brightness from loaded state
    uint8_t init_brightness = current_state.brightness_level;
    if (init_brightness < 1) init_brightness = 1;
    if (init_brightness > 10) init_brightness = 10;
    pb_set_global_brightness((uint8_t)(init_brightness * 25 + (init_brightness > 1 ? 5 : 0)));

    // Render initial view
    views_render(&display, &current_state);

    // Timing
    absolute_time_t last_tick_1s = get_absolute_time();
    absolute_time_t last_display_refresh = get_absolute_time();
    absolute_time_t last_board_addr_sample = get_absolute_time();

    printf("Entering main loop\n");

    while (true) {
        uint32_t now_us = time_us_32();
        absolute_time_t now = get_absolute_time();

        // Handle button presses
        if (select_pressed) {
            select_pressed = false;
            dispatch(action_button_select(now_us));
        }
        if (next_pressed) {
            next_pressed = false;
            dispatch(action_button_next(now_us));
        }

        // Handle IR remote commands
        uint8_t ir_code;
        if (ir_get_next_command(&ir_code)) {
            switch (ir_code) {
                case POWER:
                    dispatch(action_power_toggle(now_us));
                    break;
                case PLAY:
                    dispatch(action_fseq_next(now_us));
                    break;
                case BRIGHTNESS_UP:
                    dispatch(action_brightness_up(now_us));
                    break;
                case BRIGHTNESS_DN:
                    dispatch(action_brightness_down(now_us));
                    break;
            }
        }

        // SD Card Scan - when entering SD view OR auto-play pending on boot
        bool need_scan_for_view = current_state.in_detail_view &&
                                  current_state.menu_selection == MENU_SD_CARD &&
                                  current_state.sd_card.needs_scan;
        bool need_scan_for_autoplay = current_state.sd_card.auto_play_pending &&
                                      !current_state.sd_card.mounted;

        if (need_scan_for_view || need_scan_for_autoplay) {
            sd_ops_scan_result_t scan = sd_ops_scan_fseq_files();

            switch (scan.result) {
                case SD_OPS_OK:
                    dispatch(action_sd_card_mounted(now_us));
                    dispatch(action_sd_files_loaded(now_us, scan.file_count));
                    break;
                case SD_OPS_MOUNT_FAILED:
                    dispatch(action_sd_card_error(now_us, "Mount Failed"));
                    break;
                case SD_OPS_OPENDIR_FAILED:
                    dispatch(action_sd_card_error(now_us, "OpenDir Failed"));
                    break;
            }
        }

        // 1 second tick
        if (absolute_time_diff_us(last_tick_1s, now) >= TICK_1S_US) {
            last_tick_1s = now;
            dispatch(action_tick_1s(now_us));
        }

        // Sample board address periodically (every 100ms)
        if (absolute_time_diff_us(last_board_addr_sample, now) >= 100000) {
            last_board_addr_sample = now;

            uint16_t adc_value = sample_board_address_adc();
            uint8_t code;
            uint16_t error, margin;
            decode_board_address(adc_value, &code, &error, &margin);

            dispatch(action_board_address_updated(now_us, adc_value, code, error, margin));
        }

        // Periodic FPS update for rainbow test display
        if (current_state.in_detail_view &&
            current_state.menu_selection == MENU_RAINBOW_TEST &&
            absolute_time_diff_us(last_display_refresh, now) >= DISPLAY_REFRESH_US) {
            last_display_refresh = now;
            uint16_t fps = rainbow_test_get_fps(hw_context.rainbow_test);
            if (fps != current_state.rainbow_test.fps) {
                dispatch(action_rainbow_frame_complete(now_us, fps));
            }
        }

        // Run test module tasks (for string_test and toggle_test timing)
        side_effects_tick(&hw_context, &current_state);

        tight_loop_contents();
    }
}
