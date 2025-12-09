#include "ir_control.h"
#include <stdio.h>
#include "pico/platform.h"  // For __not_in_flash_func

// Global variables for IR control
volatile uint32_t ir_data = 0;
volatile int bit_index = -1;
volatile absolute_time_t last_fall_time;
queue_t ir_queue;
static uint ir_pin = IR_PIN;

// Track spurious/noise edges for filtering
static volatile uint32_t ir_noise_edges = 0;

// Process an IR edge (called from main's combined GPIO ISR)
// Placed in RAM to avoid flash XIP latency affecting timing-critical code
void __not_in_flash_func(ir_process_edge)(void)
{
    absolute_time_t isr_now = get_absolute_time();
    int64_t dt = absolute_time_diff_us(last_fall_time, isr_now);

    // Filter out spurious edges during frame reception:
    // - Too short (< 900us): noise/bounce - let timing accumulate
    // - Too long (> 3000us but < 10000us): also spurious - accumulate
    // Valid NEC bits are 1125us (0) or 2250us (1)
    if (bit_index >= 0)
    {
        if ((dt > 0 && dt < 900) || (dt > 3000 && dt < 10000))
        {
            ir_noise_edges++;
            return;  // Don't update last_fall_time, let timing accumulate
        }
    }

    last_fall_time = isr_now;

    // Check for leader pulse (10-20ms)
    // The remote sends ~9ms burst + ~4.5ms space = ~13.5ms between falling edges
    if (dt > 10000 && dt < 20000)
    {
        bit_index = 31;
        ir_data = 0;
    }
    else if (bit_index >= 0)
    {
        // Each bit: pulse ~562us, then space (~562us for 0, ~1690us for 1)
        // Total: ~1125us for 0, ~2250us for 1
        // Threshold at 1500us
        if (dt > 1500)
        {
            ir_data |= (1UL << bit_index);
        }
        bit_index--;

        if (bit_index == -1)
        {
            // Frame complete - verify checksum
            // Format: 0xAABBCCDD where BB=~AA and DD=~CC
            uint8_t *data = (uint8_t *)&ir_data;
            if (data[2] == (uint8_t)~data[3] && data[1] == (uint8_t)~data[0])
            {
                uint8_t code = data[1];
                queue_try_add(&ir_queue, &code);
            }
        }
    }
}

// Initialize IR control (GPIO interrupt must be enabled separately in main)
void ir_init(uint gpio_pin)
{
    ir_pin = gpio_pin;
    gpio_init(ir_pin);
    gpio_set_dir(ir_pin, GPIO_IN);
    gpio_pull_up(ir_pin);
    queue_init(&ir_queue, sizeof(uint8_t), 10);
}

// Get next command from queue
bool ir_get_next_command(uint8_t *code)
{
    return queue_try_remove(&ir_queue, code);
}
