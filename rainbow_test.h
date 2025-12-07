#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "pico/types.h"
#include "pico/time.h"
#include "pb_led_driver.h"

#define RAINBOW_TEST_NUM_STRINGS 32
#define RAINBOW_TEST_PIXELS_PER_STRING 50

typedef struct
{
    pb_driver_t *driver;
    pb_raster_t *raster;
    int raster_id;
    bool running;
    uint8_t current_string;
    uint8_t hue_offset;
    uint32_t frame_count;
    uint64_t fps_last_time_us;
    uint16_t fps;
} rainbow_test_t;

bool rainbow_test_init(rainbow_test_t *ctx, uint first_pin);
void rainbow_test_start(rainbow_test_t *ctx);
void rainbow_test_stop(rainbow_test_t *ctx);
void rainbow_test_task(rainbow_test_t *ctx);
bool rainbow_test_is_running(const rainbow_test_t *ctx);
void rainbow_test_next_string(rainbow_test_t *ctx);
uint8_t rainbow_test_get_string(const rainbow_test_t *ctx);
uint16_t rainbow_test_get_fps(const rainbow_test_t *ctx);
