#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "pico/time.h"
#include "hardware/gpio.h"

typedef struct
{
    uint base_pin;
    uint32_t mask;
    bool running;
    bool level_high;
    absolute_time_t next_toggle;
} toggle_test_t;

bool toggle_test_init(toggle_test_t *ctx, uint base_pin);
void toggle_test_start(toggle_test_t *ctx);
void toggle_test_stop(toggle_test_t *ctx);
bool toggle_test_is_running(const toggle_test_t *ctx);
void toggle_test_task(toggle_test_t *ctx);
