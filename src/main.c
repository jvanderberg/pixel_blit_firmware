#include <stdio.h>
#include <stdbool.h>
#include <limits.h>

#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/multicore.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"

// SD Card / FatFS
#include "ff.h"
#include "sd_card.h"
#include "hw_config.h"

#include "app_state.h"
#include "action.h"
#include "reducer.h"
#include "side_effects.h"
#include "views.h"

// Pin definitions
#define DISP_SDA_PIN 46
#define DISP_SCL_PIN 47
#define OLED_I2C i2c1
#define OLED_ADDR 0x3C

#define BTN_SELECT_PIN 43
#define BTN_NEXT_PIN 45

#define BOARD_ADDR_ADC_GPIO 40
#define BOARD_ADDR_ADC_CH 0
#define BOARD_ADDR_SAMPLES 32

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

// Core1 rainbow test control (extern'd by side_effects.c)
volatile bool rainbow_core1_running = false;

void core1_rainbow_entry(void) {
    while (rainbow_core1_running) {
        rainbow_test_task(&rainbow_test_ctx);
    }
}

// Button state
static volatile uint64_t select_last_press_us = 0;
static volatile uint64_t next_last_press_us = 0;
static volatile bool select_pressed = false;
static volatile bool next_pressed = false;

// Board address decoding
static const uint16_t level_codes[16] = {
    4095, 3723, 3374, 3117,
    2786, 2608, 2432, 2296,
    2048, 1950, 1850, 1770,
    1658, 1593, 1526, 1471
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
    }
    return (uint16_t)(acc / BOARD_ADDR_SAMPLES);
}

// Button ISR with debouncing
static void button_isr(uint gpio, uint32_t events) {
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

    // Setup hardware context
    hw_context.display = &display;
    hw_context.string_test = &string_test_ctx;
    hw_context.toggle_test = &toggle_test_ctx;
    hw_context.rainbow_test = &rainbow_test_ctx;

    // Initialize application state
    current_state = app_state_init();

    // Setup button interrupts
    gpio_set_irq_enabled_with_callback(BTN_SELECT_PIN, GPIO_IRQ_EDGE_FALL, true, button_isr);
    gpio_set_irq_enabled(BTN_NEXT_PIN, GPIO_IRQ_EDGE_FALL, true);

    // Render initial view
    views_render(&display, &current_state);

    // Configure SD card library to use DMA_IRQ_1 (IRQ_0 is used by pb_led_driver)
    set_spi_dma_irq_channel(true, true);  // useChannel1=true, shared=true

    // Timing
    absolute_time_t last_tick_1s = get_absolute_time();
    absolute_time_t last_display_refresh = get_absolute_time();
    absolute_time_t last_board_addr_sample = get_absolute_time();
    absolute_time_t last_sd_check = get_absolute_time();

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

        // SD Card Scan (only when in SD Menu)
        if (current_state.in_detail_view && 
            current_state.menu_selection == MENU_SD_CARD &&
            absolute_time_diff_us(last_sd_check, now) >= 1000000) {
            
            last_sd_check = now;
            
            sd_card_t *sd = sd_get_by_num(0);
            FRESULT fr = f_mount(&sd->fatfs, sd->pcName, 1);
            
            if (fr != FR_OK) {
                dispatch(action_sd_card_status(now_us, false, "Mount Failed"));
            } else {
                DIR dir;
                FILINFO fno;
                fr = f_opendir(&dir, "/");
                if (fr == FR_OK) {
                    // Read first file
                    fr = f_readdir(&dir, &fno);
                    if (fr == FR_OK && fno.fname[0]) {
                        dispatch(action_sd_card_status(now_us, true, fno.fname));
                    } else {
                        dispatch(action_sd_card_status(now_us, true, "Empty Dir"));
                    }
                    f_closedir(&dir);
                } else {
                    dispatch(action_sd_card_status(now_us, true, "OpenDir Failed"));
                }
                // f_unmount(sd->pcName); // Optional, keep mounted for now
            }
        }

        // 1 second tick
        if (absolute_time_diff_us(last_tick_1s, now) >= TICK_1S_US) {
            last_tick_1s = now;
            dispatch(action_tick_1s(now_us));

            // Log state periodically
            printf("Menu=%d View=%s Uptime=%lu\n",
                   current_state.menu_selection,
                   current_state.in_detail_view ? "Detail" : "Menu",
                   (unsigned long)current_state.uptime_seconds);
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

        // Rainbow test runs on core1 - nothing to do here

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
