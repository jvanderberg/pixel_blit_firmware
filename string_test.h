#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "pico/types.h"
#include "pico/time.h"
#include "hardware/pio.h"

typedef struct
{
    PIO pio;
    uint sm;
    uint offset;
    uint first_pin;
    bool running;
    absolute_time_t next_update;
    uint32_t output_state;
    uint16_t counters[32];
} string_test_t;

bool string_test_init(string_test_t *ctx, uint first_pin);
void string_test_start(string_test_t *ctx);
void string_test_stop(string_test_t *ctx);
void string_test_task(string_test_t *ctx);
bool string_test_is_running(const string_test_t *ctx);
