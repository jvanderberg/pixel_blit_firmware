#include "ir_control.h"
#include <stdio.h>

// Global variables for IR control
volatile uint32_t ir_data = 0;
volatile int bit_index = 0;
volatile absolute_time_t last_fall_time;
volatile uint32_t ir_edge_count = 0;  // Debug: count edges
queue_t ir_queue;
static uint ir_pin = IR_PIN; // Initialize with default pin

// IR GPIO callback function
void gpio_callback(uint gpio, uint32_t events)
{
    if (gpio != ir_pin || !(events & GPIO_IRQ_EDGE_FALL))
    {
        return;
    }

    ir_edge_count++;  // Debug: count every falling edge

    absolute_time_t now = get_absolute_time();
    int64_t dt = absolute_time_diff_us(last_fall_time, now);
    last_fall_time = now;

    // The remote sends an 'attention' or frame reset which is a long pulse, which is ~9ms, then a short pulse of ~4.5ms,
    // which adds up to ~13ms between falling edges. So here we just filter for something in about the right range. This signals
    // that we are starting a new frame, and we need to reset the bit index and clear the ir_data.
    if (dt > 10000 && dt < 20000)
    {
        bit_index = 31;
        ir_data = 0;
    }
    else if (bit_index >= 0)
    {
        // Each bit: pulse ~562us, then space, which is either ~562us or ~1690us, so a
        // low to low spaces is 1124us for a 0, or 2252us for a 1 see: https://www.vishay.com/docs/80071/dataform.pdf (page 2)
        // So if dt is > 1750us, it's a 1, otherwise it's a 0 (1700 is roughtly half way between and a nice round number)
        if (dt > 1700)
        {
            // Long space → "1" bit
            ir_data |= (1UL << bit_index);
        }
        else
        {
            // Short space → "0" bit (already zeroed)
        }
        bit_index--;

        if (bit_index == -1)
        {
            // We've gotten all 32 bits, they are in the format 0xAABBCCDD, where AA is the address which is always
            // 0x00, BB is the inverse of AA, CC is the command, and DD is the inverse of CC.
            // So for example, POWER is 0x00FF02FD. We verify that 0xFF == ~0x00 and 0x02 == ~0xFD, and then we can just take the
            // command byte (0x02) and send it to the queue.
            uint8_t *data = (uint8_t *)&ir_data;
            if (data[2] == (uint8_t)~data[3] && data[1] == (uint8_t)~data[0])
            {
                uint8_t code = data[1];
                queue_try_add(&ir_queue, &code);
            }
        }
    }
}

// Debug: get edge count
uint32_t ir_get_edge_count(void) {
    return ir_edge_count;
}

// Process an IR edge (called from main's combined GPIO ISR)
void ir_process_edge(void)
{
    ir_edge_count++;

    absolute_time_t now = get_absolute_time();
    int64_t dt = absolute_time_diff_us(last_fall_time, now);
    last_fall_time = now;

    if (dt > 10000 && dt < 20000)
    {
        bit_index = 31;
        ir_data = 0;
    }
    else if (bit_index >= 0)
    {
        if (dt > 1500)
        {
            ir_data |= (1UL << bit_index);
        }
        bit_index--;

        if (bit_index == -1)
        {
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
    // Note: IRQ is enabled in main.c's combined callback
    queue_init(&ir_queue, sizeof(uint8_t), 10);
}

// Get next command from queue
bool ir_get_next_command(uint8_t *code)
{
    return queue_try_remove(&ir_queue, code);
}

// Handle IR command
void ir_handle_command(uint8_t code)
{
    switch (code)
    {
    case BRIGHTNESS_UP:
        // TODO: Implement brightness up
        break;
    case BRIGHTNESS_DN:
        // TODO: Implement brightness down
        break;
    case PLAY:
        // TODO: Implement play/pause
        break;
    case POWER:
        // TODO: Implement power toggle
        break;
    case RED1:
    case RED2:
    case RED3:
    case RED4:
    case RED5:
        // TODO: Implement red color
        break;
    case GREEN1:
    case GREEN2:
    case GREEN3:
    case GREEN4:
    case GREEN5:
        // TODO: Implement green color
        break;
    case BLUE1:
    case BLUE2:
    case BLUE3:
    case BLUE4:
    case BLUE5:
        // TODO: Implement blue color
        break;
    case WHITE1:
    case WHITE2:
    case WHITE3:
    case WHITE4:
    case WHITE5:
        // TODO: Implement white color
        break;
    case QUICK:
        // TODO: Implement quick mode
        break;
    case SLOW:
        // TODO: Implement slow mode
        break;
    case AUTO:
        // TODO: Implement auto mode
        break;
    case FLASH:
        // TODO: Implement flash mode
        break;
    case JUMP3:
    case JUMP7:
        // TODO: Implement jump mode
        break;
    case FADE3:
    case FADE7:
        // TODO: Implement fade mode
        break;
    default:
        break;
    }
}