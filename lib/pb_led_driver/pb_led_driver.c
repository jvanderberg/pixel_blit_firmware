/**
 * pb_led_driver.c - Core driver implementation
 */

#include "pb_led_driver.h"
#include <string.h>

// ============================================================================
// Internal driver structure
// ============================================================================

struct pb_driver {
    pb_driver_config_t config;

    // Bit-plane encoded buffers
    // Layout: buffers[buffer_index][pixel * 3 + channel]
    // Double-buffered: buffer 0 = front (DMA), buffer 1 = back (write)
    pb_value_bits_t* buffers;
    size_t buffer_size;          // Size of one buffer in value_bits_t units
    uint8_t current_buffer;      // Which buffer is back buffer (0 or 1)

    // Statistics
    uint32_t frame_count;
    uint16_t fps;
};

// ============================================================================
// Static allocation
// ============================================================================

// Single driver instance (no dynamic allocation)
static struct pb_driver driver_instance;
static bool driver_in_use = false;

// Static buffer storage: max_pixels * 3 channels * 2 buffers
static pb_value_bits_t buffer_storage[PB_MAX_PIXELS * 3 * 2];

// ============================================================================
// Helper functions
// ============================================================================

static size_t calc_buffer_size(const pb_driver_config_t* config) {
    // max_pixels * 3 channels
    return (size_t)config->max_pixel_length * 3;
}

// Get pointer to a buffer
static pb_value_bits_t* get_board_buffer(pb_driver_t* driver, uint8_t board, uint8_t buffer_idx) {
    (void)board;  // Single board design
    size_t buffer_offset = buffer_idx * calc_buffer_size(&driver->config);
    return &driver->buffers[buffer_offset];
}

// ============================================================================
// Driver lifecycle
// ============================================================================

#ifndef PB_LED_DRIVER_TEST_BUILD
// External hardware init function
extern int pb_hw_init(pb_driver_t* driver);
extern void pb_hw_deinit(void);
#endif

pb_driver_t* pb_driver_init(const pb_driver_config_t* config) {
    if (config == NULL) return NULL;
    if (config->num_strings == 0 || config->num_strings > PB_MAX_STRINGS) return NULL;
    if (config->max_pixel_length == 0 || config->max_pixel_length > PB_MAX_PIXELS) return NULL;

    // Only one driver instance allowed
    if (driver_in_use) return NULL;

    pb_driver_t* driver = &driver_instance;
    memset(driver, 0, sizeof(pb_driver_t));
    memcpy(&driver->config, config, sizeof(pb_driver_config_t));

    // Use static buffer storage
    driver->buffer_size = calc_buffer_size(config);
    driver->buffers = buffer_storage;
    memset(driver->buffers, 0, driver->buffer_size * 2 * sizeof(pb_value_bits_t));

    driver->current_buffer = 0;
    driver->frame_count = 0;
    driver->fps = 0;

#ifndef PB_LED_DRIVER_TEST_BUILD
    // Reset timing state for FPS tracking (static variables persist across driver cycles)
    extern void pb_reset_timing_state(void);
    pb_reset_timing_state();

    // Initialize hardware (PIO/DMA)
    if (pb_hw_init(driver) != 0) {
        return NULL;
    }
#endif

    driver_in_use = true;
    return driver;
}

void pb_driver_deinit(pb_driver_t* driver) {
    if (driver == NULL) return;
    if (driver != &driver_instance) return;  // Not our instance

    // Clean up all rasters associated with this driver
    pb_raster_destroy_all(driver);

#ifndef PB_LED_DRIVER_TEST_BUILD
    // Deinitialize hardware
    pb_hw_deinit();
#endif

    driver_in_use = false;
}

const pb_driver_config_t* pb_driver_get_config(const pb_driver_t* driver) {
    if (driver == NULL) return NULL;
    return &driver->config;
}

// ============================================================================
// Bit-plane encoding
// ============================================================================

void pb_set_pixel(pb_driver_t* driver, uint8_t board, uint8_t string,
                  uint16_t pixel, pb_color_t color) {
    if (driver == NULL) return;
    if (board >= driver->config.num_boards) return;
    if (string >= driver->config.num_strings) return;
    if (pixel >= driver->config.max_pixel_length) return;

    // Get back buffer for this board
    pb_value_bits_t* buffer = get_board_buffer(driver, board, driver->current_buffer);

    // Extract color components (in RGB order from color)
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;

    // Reorder based on color_order setting
    uint8_t channels[3];
    switch (driver->config.color_order) {
        case PB_COLOR_ORDER_GRB: channels[0] = g; channels[1] = r; channels[2] = b; break;
        case PB_COLOR_ORDER_RGB: channels[0] = r; channels[1] = g; channels[2] = b; break;
        case PB_COLOR_ORDER_BGR: channels[0] = b; channels[1] = g; channels[2] = r; break;
        case PB_COLOR_ORDER_RBG: channels[0] = r; channels[1] = b; channels[2] = g; break;
        case PB_COLOR_ORDER_GBR: channels[0] = g; channels[1] = b; channels[2] = r; break;
        case PB_COLOR_ORDER_BRG: channels[0] = b; channels[1] = r; channels[2] = g; break;
        default: channels[0] = g; channels[1] = r; channels[2] = b; break;
    }

    // Bit mask for this string
    uint32_t mask = 1u << string;

    // Encode each channel into bit planes
    size_t base_idx = (size_t)pixel * 3;
    for (int ch = 0; ch < 3; ch++) {
        uint8_t value = channels[ch];
        pb_value_bits_t* dest = &buffer[base_idx + ch];

        for (int bit = 0; bit < 8; bit++) {
            uint32_t color_bit = (value >> (7 - bit)) & 1;
            if (color_bit) {
                dest->planes[bit] |= mask;
            } else {
                dest->planes[bit] &= ~mask;
            }
        }
    }
}

pb_color_t pb_get_pixel(const pb_driver_t* driver, uint8_t board,
                        uint8_t string, uint16_t pixel) {
    if (driver == NULL) return 0;
    if (board >= driver->config.num_boards) return 0;
    if (string >= driver->config.num_strings) return 0;
    if (pixel >= driver->config.max_pixel_length) return 0;

    // Get back buffer for this board (cast away const for helper)
    pb_value_bits_t* buffer = get_board_buffer((pb_driver_t*)driver, board,
                                                ((pb_driver_t*)driver)->current_buffer);

    // Bit mask for this string
    uint32_t mask = 1u << string;

    // Decode each channel from bit planes
    size_t base_idx = (size_t)pixel * 3;
    uint8_t channels[3] = {0, 0, 0};

    for (int ch = 0; ch < 3; ch++) {
        pb_value_bits_t* src = &buffer[base_idx + ch];
        uint8_t value = 0;

        for (int bit = 0; bit < 8; bit++) {
            if (src->planes[bit] & mask) {
                value |= (1 << (7 - bit));
            }
        }
        channels[ch] = value;
    }

    // Reorder back to RGB based on color_order setting
    uint8_t r, g, b;
    switch (driver->config.color_order) {
        case PB_COLOR_ORDER_GRB: g = channels[0]; r = channels[1]; b = channels[2]; break;
        case PB_COLOR_ORDER_RGB: r = channels[0]; g = channels[1]; b = channels[2]; break;
        case PB_COLOR_ORDER_BGR: b = channels[0]; g = channels[1]; r = channels[2]; break;
        case PB_COLOR_ORDER_RBG: r = channels[0]; b = channels[1]; g = channels[2]; break;
        case PB_COLOR_ORDER_GBR: g = channels[0]; b = channels[1]; r = channels[2]; break;
        case PB_COLOR_ORDER_BRG: b = channels[0]; r = channels[1]; g = channels[2]; break;
        default: g = channels[0]; r = channels[1]; b = channels[2]; break;
    }

    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

void pb_clear_board(pb_driver_t* driver, uint8_t board, pb_color_t color) {
    if (driver == NULL) return;
    if (board >= driver->config.num_boards) return;

    for (uint8_t s = 0; s < driver->config.num_strings; s++) {
        for (uint16_t p = 0; p < driver->config.max_pixel_length; p++) {
            pb_set_pixel(driver, board, s, p, color);
        }
    }
}

void pb_clear_all(pb_driver_t* driver, pb_color_t color) {
    if (driver == NULL) return;

    uint8_t num_boards = (driver->config.board_id == 0) ? driver->config.num_boards : 1;
    for (uint8_t b = 0; b < num_boards; b++) {
        pb_clear_board(driver, b, color);
    }
}

// ============================================================================
// Buffer access (for hardware layer)
// ============================================================================

pb_value_bits_t* pb_driver_get_front_buffer(pb_driver_t* driver) {
    if (driver == NULL) return NULL;
    // Front buffer is the opposite of current (back) buffer
    uint8_t front = driver->current_buffer ^ 1;
    return get_board_buffer(driver, 0, front);
}

// ============================================================================
// Show implementation
// ============================================================================

#ifdef PB_LED_DRIVER_TEST_BUILD

// Test build stubs
void pb_show(pb_driver_t* driver) {
    (void)driver;
}

void pb_show_async(pb_driver_t* driver) {
    (void)driver;
}

void pb_show_wait(pb_driver_t* driver) {
    (void)driver;
}

bool pb_show_busy(const pb_driver_t* driver) {
    (void)driver;
    return false;
}

void pb_show_with_fps(pb_driver_t* driver, uint16_t target_fps) {
    (void)driver; (void)target_fps;
}

uint16_t pb_get_fps(const pb_driver_t* driver) {
    (void)driver;
    return 0;
}

uint32_t pb_get_frame_count(const pb_driver_t* driver) {
    (void)driver;
    return 0;
}

#else // Hardware build

#include "pico/time.h"

// External hardware functions
extern bool pb_hw_show(pb_driver_t* driver, bool blocking);
extern bool pb_hw_show_busy(void);
extern void pb_hw_show_wait(void);

static uint64_t last_show_time = 0;
static uint64_t fps_window_start = 0;
static uint32_t fps_frame_count = 0;

// Called from pb_driver_init to reset timing state for new driver instance
void pb_reset_timing_state(void) {
    last_show_time = 0;
    fps_window_start = 0;
    fps_frame_count = 0;
}

void pb_show(pb_driver_t* driver) {
    if (driver == NULL) return;

    // Swap buffers: current back buffer becomes front buffer for DMA
    driver->current_buffer ^= 1;

    // Trigger DMA (blocking - always succeeds)
    (void)pb_hw_show(driver, true);

    driver->frame_count++;

    // Calculate FPS averaged over 1 second
    uint64_t now = time_us_64();
    fps_frame_count++;
    if (fps_window_start == 0) {
        fps_window_start = now;
    }
    uint64_t elapsed = now - fps_window_start;
    if (elapsed >= 1000000) {
        driver->fps = (uint16_t)((fps_frame_count * 1000000ULL) / elapsed);
        fps_frame_count = 0;
        fps_window_start = now;
    }
    last_show_time = now;
}

void pb_show_async(pb_driver_t* driver) {
    if (driver == NULL) return;

    // Swap buffers BEFORE triggering DMA so front buffer is correct
    driver->current_buffer ^= 1;

    // Trigger DMA (non-blocking)
    if (!pb_hw_show(driver, false)) {
        // DMA busy - swap back to avoid writing to buffer being DMA'd
        driver->current_buffer ^= 1;
        return;
    }

    driver->frame_count++;
}

void pb_show_wait(pb_driver_t* driver) {
    (void)driver;
    pb_hw_show_wait();
}

bool pb_show_busy(const pb_driver_t* driver) {
    (void)driver;
    return pb_hw_show_busy();
}

void pb_show_with_fps(pb_driver_t* driver, uint16_t target_fps) {
    if (driver == NULL || target_fps == 0) return;

    uint64_t target_interval_us = 1000000 / target_fps;

    // Wait for target interval: sleep while far away, tight-spin at the end
    if (last_show_time > 0) {
        uint64_t target_time = last_show_time + target_interval_us;
        // Sleep in 100µs chunks until we're within 200µs
        while (time_us_64() + 200 < target_time) {
            sleep_us(100);
        }
        // Tight spin for final precision
        while (time_us_64() < target_time) {
            tight_loop_contents();
        }
    }

    pb_show(driver);
}

uint16_t pb_get_fps(const pb_driver_t* driver) {
    if (driver == NULL) return 0;
    return driver->fps;
}

uint32_t pb_get_frame_count(const pb_driver_t* driver) {
    if (driver == NULL) return 0;
    return driver->frame_count;
}

#endif // PB_LED_DRIVER_TEST_BUILD
