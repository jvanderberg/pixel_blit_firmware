#include <stdio.h>
#include <stdbool.h>
#include <limits.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "sh1106.h"
#include "string_test.h"
#include "toggle_test.h"
#include "rainbow_test.h"

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

typedef enum
{
    MENU_INFO = 0,
    MENU_BOARD_ADDRESS,
    MENU_STRING_TEST,
    MENU_TOGGLE_TEST,
    MENU_RAINBOW_TEST,
    MENU_COUNT
} menu_entry_t;

typedef struct
{
    uint8_t code;
    uint16_t best_error;
    uint16_t margin;
} board_addr_result_t;

#define BTN_DEBOUNCE_US 200000  // 200ms debounce

static volatile bool select_pressed = false;
static volatile bool next_pressed = false;
static volatile bool redraw_menu = true;
static volatile bool in_detail_view = false;
static menu_entry_t current_selection = MENU_INFO;
static volatile uint64_t select_last_press_us = 0;
static volatile uint64_t next_last_press_us = 0;

static uint16_t board_address_value = 0;
static board_addr_result_t board_address_result;
static uint16_t last_board_address_value_rendered = UINT16_MAX;
static uint8_t last_board_address_code_rendered = 0xFF;
static uint16_t last_board_address_error_rendered = UINT16_MAX;
static uint16_t last_board_address_margin_rendered = UINT16_MAX;
static string_test_t string_test_ctx;
static toggle_test_t toggle_test_ctx;
static rainbow_test_t rainbow_test_ctx;

static void button_isr(uint gpio, uint32_t events)
{
    uint64_t now = time_us_64();

    if (gpio == BTN_SELECT_PIN && (events & GPIO_IRQ_EDGE_FALL))
    {
        if (now - select_last_press_us >= BTN_DEBOUNCE_US)
        {
            select_pressed = true;
            select_last_press_us = now;
        }
    }
    else if (gpio == BTN_NEXT_PIN && (events & GPIO_IRQ_EDGE_FALL))
    {
        if (now - next_last_press_us >= BTN_DEBOUNCE_US)
        {
            next_pressed = true;
            next_last_press_us = now;
        }
    }
}

static inline uint16_t absdiff_u16(uint16_t a, uint16_t b)
{
    return (a > b) ? (uint16_t)(a - b) : (uint16_t)(b - a);
}

static const uint16_t level_codes[16] = {
    4095, 3723, 3374, 3117,
    2786, 2608, 2432, 2296,
    2048, 1950, 1850, 1770,
    1658, 1593, 1526, 1471};

static const uint8_t code_by_rank[16] = {
    0x0, 0x8, 0x4, 0xC,
    0x2, 0xA, 0x6, 0xE,
    0x1, 0x9, 0x5, 0xD,
    0x3, 0xB, 0x7, 0xF};

static board_addr_result_t decode_board_address(uint16_t sample)
{
    board_addr_result_t result = {0};
    uint16_t best_err = absdiff_u16(level_codes[0], sample);
    uint16_t next_err = UINT16_MAX;
    int best_rank = 0;

    for (int i = 1; i < 16; i++)
    {
        uint16_t err = absdiff_u16(level_codes[i], sample);
        if (err < best_err)
        {
            next_err = best_err;
            best_err = err;
            best_rank = i;
        }
        else if (err < next_err)
        {
            next_err = err;
        }
    }

    result.code = code_by_rank[best_rank];
    result.best_error = best_err;
    result.margin = (next_err == UINT16_MAX) ? 0 : (uint16_t)(next_err - best_err);
    return result;
}

static uint16_t sample_board_address_adc(void)
{
    uint32_t acc = 0;
    adc_select_input(BOARD_ADDR_ADC_CH);
    for (int i = 0; i < BOARD_ADDR_SAMPLES; i++)
    {
        acc += adc_read();
    }
    return (uint16_t)(acc / BOARD_ADDR_SAMPLES);
}

static void draw_info_detail(sh1106_t *display)
{
    char line[24];
    uint32_t uptime_s = to_ms_since_boot(get_absolute_time()) / 1000;
    sh1106_clear(display);
    sh1106_draw_string(display, 0, 0, "PixelBlit", false);
    sh1106_draw_string(display, 0, 8, "pixelblit_v4", false);
    snprintf(line, sizeof line, "Uptime: %lus", (unsigned long)uptime_s);
    sh1106_draw_string(display, 0, 24, line, false);
    snprintf(line, sizeof line, "Board ADC: %u", board_address_value);
    sh1106_draw_string(display, 0, 32, line, false);
    sh1106_draw_string(display, 0, 40, "Next exits", false);
    sh1106_render(display);
}

static void draw_board_address_detail(sh1106_t *display)
{
    char line[24];
    sh1106_clear(display);
    sh1106_draw_string(display, 0, 0, "Board Address", false);
    snprintf(line, sizeof line, "ADC: %u", board_address_value);
    sh1106_draw_string(display, 0, 16, line, false);
    snprintf(line, sizeof line, "Code: 0x%X", board_address_result.code);
    sh1106_draw_string(display, 0, 24, line, false);
    snprintf(line, sizeof line, "Err:%u M:%u", board_address_result.best_error,
             board_address_result.margin);
    sh1106_draw_string(display, 0, 32, line, false);
    sh1106_draw_string(display, 0, 48, "Next exits", false);
    sh1106_render(display);
}

static void draw_string_test_detail(sh1106_t *display, bool running)
{
    sh1106_clear(display);
    sh1106_draw_string(display, 0, 0, "String Test", false);
    sh1106_draw_string(display, 0, 16, running ? "RUNNING" : "STOPPED", running);
    sh1106_draw_string(display, 0, 32, "Select toggles", false);
    sh1106_draw_string(display, 0, 48, "Next exits", false);
    sh1106_render(display);
}

static void draw_toggle_test_detail(sh1106_t *display, bool running)
{
    sh1106_clear(display);
    sh1106_draw_string(display, 0, 0, "Toggle Test", false);
    sh1106_draw_string(display, 0, 16, running ? "RUNNING" : "STOPPED", running);
    sh1106_draw_string(display, 0, 32, "Select toggles", false);
    sh1106_draw_string(display, 0, 48, "Next exits", false);
    sh1106_render(display);
}

static void draw_rainbow_test_detail(sh1106_t *display, bool running, uint8_t string_num, uint16_t fps)
{
    char line[24];
    sh1106_clear(display);
    sh1106_draw_string(display, 0, 0, "Rainbow Test", false);
    snprintf(line, sizeof line, "String: %u  FPS: %u", string_num, fps);
    sh1106_draw_string(display, 0, 16, line, false);
    sh1106_draw_string(display, 0, 24, running ? "RUNNING" : "STOPPED", running);
    sh1106_draw_string(display, 0, 40, "Select: next str", false);
    sh1106_draw_string(display, 0, 48, "Next: exit", false);
    sh1106_render(display);
}

static void draw_menu(sh1106_t *display)
{
    sh1106_clear(display);
    sh1106_draw_string(display, 0, 0, "Pixel Blit v1.1", false);
    sh1106_draw_string(display, 0, 10, "Info", current_selection == MENU_INFO);
    sh1106_draw_string(display, 0, 20, "Board Address", current_selection == MENU_BOARD_ADDRESS);
    sh1106_draw_string(display, 0, 30, "String Test", current_selection == MENU_STRING_TEST);
    sh1106_draw_string(display, 0, 40, "Toggle Test", current_selection == MENU_TOGGLE_TEST);
    sh1106_draw_string(display, 0, 50, "Rainbow Test", current_selection == MENU_RAINBOW_TEST);
    sh1106_render(display);
}

int main()
{
    stdio_init_all();
    sleep_ms(2000);
    printf("Pixel_Blit display smoke test starting...\n");

    i2c_init(OLED_I2C, 400 * 1000);
    gpio_set_function(DISP_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(DISP_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(DISP_SDA_PIN);
    gpio_pull_up(DISP_SCL_PIN);

    sh1106_t display;
    bool ready = sh1106_init(&display, OLED_I2C, OLED_ADDR);
    if (ready)
    {
        sh1106_clear(&display);
        sh1106_draw_string(&display, 0, 0, "PixelBlit", false);
        sh1106_draw_string(&display, 0, 8, "Display OK", false);
        sh1106_render(&display);
        printf("OLED initialized at 0x%02X\n", OLED_ADDR);
    }
    else
    {
        printf("Failed to init OLED at 0x%02X\n", OLED_ADDR);
    }

    gpio_init(BTN_SELECT_PIN);
    gpio_set_function(BTN_SELECT_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(BTN_SELECT_PIN, GPIO_IN);
    gpio_pull_up(BTN_SELECT_PIN);

    gpio_init(BTN_NEXT_PIN);
    gpio_set_function(BTN_NEXT_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(BTN_NEXT_PIN, GPIO_IN);
    gpio_pull_up(BTN_NEXT_PIN);

    adc_init();
    adc_gpio_init(BOARD_ADDR_ADC_GPIO);

    if (!string_test_init(&string_test_ctx, STRING_OUT_BASE_PIN))
    {
        printf("String test init failed\n");
    }
    if (!toggle_test_init(&toggle_test_ctx, STRING_OUT_BASE_PIN))
    {
        printf("Toggle test init failed\n");
    }
    if (!rainbow_test_init(&rainbow_test_ctx, STRING_OUT_BASE_PIN))
    {
        printf("Rainbow test init failed\n");
    }

    gpio_set_irq_enabled_with_callback(BTN_SELECT_PIN,
                                       GPIO_IRQ_EDGE_FALL,
                                       true, button_isr);
    gpio_set_irq_enabled(BTN_NEXT_PIN, GPIO_IRQ_EDGE_FALL, true);

    absolute_time_t last_log = get_absolute_time();
    absolute_time_t last_rainbow_display_update = get_absolute_time();

    while (true)
    {
        board_address_value = sample_board_address_adc();
        board_address_result = decode_board_address(board_address_value);

        bool board_addr_changed =
            board_address_value != last_board_address_value_rendered ||
            board_address_result.code != last_board_address_code_rendered ||
            board_address_result.best_error != last_board_address_error_rendered ||
            board_address_result.margin != last_board_address_margin_rendered;

        if (board_addr_changed)
        {
            last_board_address_value_rendered = board_address_value;
            last_board_address_code_rendered = board_address_result.code;
            last_board_address_error_rendered = board_address_result.best_error;
            last_board_address_margin_rendered = board_address_result.margin;

            if (in_detail_view && current_selection == MENU_BOARD_ADDRESS)
            {
                redraw_menu = true;
            }
        }

        // Periodic display refresh for rainbow test FPS
        if (in_detail_view && current_selection == MENU_RAINBOW_TEST &&
            absolute_time_diff_us(last_rainbow_display_update, get_absolute_time()) >= 500000)
        {
            last_rainbow_display_update = get_absolute_time();
            redraw_menu = true;
        }

        if (select_pressed)
        {
            select_pressed = false;
            if (in_detail_view)
            {
                if (current_selection == MENU_STRING_TEST)
                {
                    string_test_stop(&string_test_ctx);
                    in_detail_view = false;
                }
                else if (current_selection == MENU_TOGGLE_TEST)
                {
                    toggle_test_stop(&toggle_test_ctx);
                    in_detail_view = false;
                }
                else if (current_selection == MENU_RAINBOW_TEST)
                {
                    // Select advances to next string (doesn't exit)
                    rainbow_test_next_string(&rainbow_test_ctx);
                }
                else
                {
                    in_detail_view = false;
                }
            }
            else
            {
                in_detail_view = true;
                if (current_selection == MENU_STRING_TEST)
                {
                    toggle_test_stop(&toggle_test_ctx);
                    rainbow_test_stop(&rainbow_test_ctx);
                    string_test_start(&string_test_ctx);
                }
                else if (current_selection == MENU_TOGGLE_TEST)
                {
                    string_test_stop(&string_test_ctx);
                    rainbow_test_stop(&rainbow_test_ctx);
                    toggle_test_start(&toggle_test_ctx);
                }
                else if (current_selection == MENU_RAINBOW_TEST)
                {
                    string_test_stop(&string_test_ctx);
                    toggle_test_stop(&toggle_test_ctx);
                    rainbow_test_start(&rainbow_test_ctx);
                }
            }
            redraw_menu = true;
        }
        if (next_pressed)
        {
            next_pressed = false;
            if (in_detail_view)
            {
                if (current_selection == MENU_STRING_TEST)
                {
                    string_test_stop(&string_test_ctx);
                }
                else if (current_selection == MENU_TOGGLE_TEST)
                {
                    toggle_test_stop(&toggle_test_ctx);
                }
                else if (current_selection == MENU_RAINBOW_TEST)
                {
                    rainbow_test_stop(&rainbow_test_ctx);
                }
                in_detail_view = false;
            }
            else
            {
                current_selection = (menu_entry_t)((current_selection + 1) % MENU_COUNT);
            }
            redraw_menu = true;
        }

        if (redraw_menu)
        {
            redraw_menu = false;
            if (in_detail_view)
            {
                if (current_selection == MENU_INFO)
                {
                    draw_info_detail(&display);
                }
                else if (current_selection == MENU_BOARD_ADDRESS)
                {
                    draw_board_address_detail(&display);
                }
                else if (current_selection == MENU_STRING_TEST)
                {
                    draw_string_test_detail(&display, string_test_is_running(&string_test_ctx));
                }
                else if (current_selection == MENU_TOGGLE_TEST)
                {
                    draw_toggle_test_detail(&display, toggle_test_is_running(&toggle_test_ctx));
                }
                else if (current_selection == MENU_RAINBOW_TEST)
                {
                    draw_rainbow_test_detail(&display, rainbow_test_is_running(&rainbow_test_ctx),
                                             rainbow_test_get_string(&rainbow_test_ctx),
                                             rainbow_test_get_fps(&rainbow_test_ctx));
                }
            }
            else
            {
                draw_menu(&display);
            }
        }

        absolute_time_t now = get_absolute_time();
        if (absolute_time_diff_us(last_log, now) >= 1000000)
        {
            const char *entry_name = "Info";
            if (current_selection == MENU_BOARD_ADDRESS)
            {
                entry_name = "Board Addr";
            }
            else if (current_selection == MENU_STRING_TEST)
            {
                entry_name = "String Test";
            }
            else if (current_selection == MENU_TOGGLE_TEST)
            {
                entry_name = "Toggle Test";
            }
            else if (current_selection == MENU_RAINBOW_TEST)
            {
                entry_name = "Rainbow Test";
            }

            printf("Menu=%s View=%s ADC=%u Code=0x%X Err=%u Margin=%u\n",
                   entry_name,
                   in_detail_view ? "Detail" : "Menu",
                   board_address_value,
                   board_address_result.code,
                   board_address_result.best_error,
                   board_address_result.margin);
            last_log = now;
        }
        string_test_task(&string_test_ctx);
        toggle_test_task(&toggle_test_ctx);
        rainbow_test_task(&rainbow_test_ctx);
        tight_loop_contents();
    }
}
