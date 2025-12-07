/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/vreg.h"
#include "hardware/gpio.h"
#include "pico/util/queue.h"
#include "pico/multicore.h"
#include <string.h>
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "pico/sem.h"
#include <inttypes.h>
#define PIO_RXOVER_MASK(sm) (1u << (8 + (sm)))
#define DMA_CHANNEL0_MASK (1u << DMA_CHANNEL0)
#define SYS_CLOCK_KHZ 360000

#define TX_CLOCK_DIV 4

//  Uncomment one of these to select the mode
// #define PARALLEL_SENDER
#define PARALLEL_RECEIVER

#define BUFFER_SIZE 1024
#define TX_DATA 0xAAAAAAAA
#define BOARD_HEADER 0x0
#define STRING_HEADER (1 << 30)
#define PIXEL_HEADER (2 << 30)
#define SYNC_HEADER (3 << 30)

#ifdef PARALLEL_SENDER
#include "parallel_out.pio.h" // Sender PIO program
#else
#include "parallel_in.pio.h"  // Receiver PIO program
#include "board_filter.pio.h" // Board filter PIO program
#endif

// Pico W devices use a GPIO on the WIFI chip for the LED,
// so when building for Pico W, CYW43_WL_GPIO_LED_PIN will be defined
#ifdef CYW43_WL_GPIO_LED_PIN
#include "pico/cyw43_arch.h"
#endif

volatile bool data_ready = false;
uint32_t failures = 0;
uint32_t successes = 0;
uint32_t interrupted = 0;
// PIO state machine configuration
static PIO pio = pio0;
static uint sm = 0;
static uint sm_filter = 1;
static uint offset = 0;
static uint32_t counter = 0;

char *data_string = "Hello, World";
char *received_data = "                                ";
uint8_t data_string_len = 12;
uint8_t data_string_index = 0;
char string_buffer[100];
bool start_data = false;

// Double buffer for received data
uint32_t received_data_buffer[BUFFER_SIZE * 2];
int current_buffer = 0;
int buffer_index = 0;
volatile uint32_t sample = 0;

// DMA channel for PIO RX FIFO
#define DMA_CHANNEL0 0
#define DMA_FILTER 2
#define DMA_IRQ DMA_IRQ_0
#define DMA_IRQ_FILTER DMA_IRQ_1
#define BOARD_ID 12
// DMA control block
dma_channel_config dma_config0;
dma_channel_config dma_config_filter;

// Add debug counters at the top with other globals
volatile uint32_t dma_transfers = 0;

volatile uint32_t bypass = 0;

void __isr dma_irq_handler()
{

    if (dma_transfers % 10000 == 0)
    {
        gpio_xor_mask(1u << 16); // Toggle LED
    }

    if (dma_hw->ints0 & DMA_CHANNEL0_MASK)
    {
        // clear IRQ
        dma_hw->ints0 = 1u << DMA_CHANNEL0;
        dma_channel_set_write_addr(DMA_CHANNEL0, &pio0_hw->txf[sm_filter], false);
        dma_channel_set_trans_count(DMA_CHANNEL0, BUFFER_SIZE, true);
    }
}

void __isr dma_irq_filter_handler()
{
    dma_transfers++;

    if (dma_hw->ints1 & 4)
    {

        // clear IRQ
        dma_hw->ints1 = 4;
        if (multicore_fifo_wready())
        {

            multicore_fifo_push_blocking(current_buffer);
        }
        else
        {
            interrupted++;
        }
        current_buffer = 1 - current_buffer;
        dma_channel_set_write_addr(DMA_FILTER, &received_data_buffer[current_buffer * BUFFER_SIZE], false);
        dma_channel_set_trans_count(DMA_FILTER, BUFFER_SIZE, true);
    }
}
volatile uint8_t board_id = 0;
volatile uint8_t string_id = 0;
volatile bool board_header_sent = false;
volatile bool string_header_sent = false;
void pio_irq_handler()
{

    // Clear the IRQ flag
    pio0_hw->irq = 1u << sm;

    while (!pio_sm_is_tx_fifo_full(pio, sm))
    {

        uint32_t board_id_header = BOARD_HEADER | board_id << 22;
        uint32_t string_header = STRING_HEADER | string_id << 22;
        if (board_header_sent == false)
        {
            pio_sm_put(pio0, sm, board_id_header);
            board_header_sent = true;
            continue;
        }
        if (string_header_sent == false && board_header_sent == true)
        {
            pio_sm_put(pio0, sm, string_header);
            string_header_sent = true;
            continue;
        }

        uint32_t data = PIXEL_HEADER | data_string[data_string_index] << 16 | data_string[data_string_index + 1] << 8 | data_string[data_string_index + 2];
        pio_sm_put(pio0, sm, data);
        data_string_index += 3;
        if (data_string_index >= data_string_len)
        {

            data_string_index = 0;
            board_id++;
            board_header_sent = false;
            string_header_sent = false;
            if (board_id > 15)
            {
                board_id = 0;
            }
        }
    }
}

// Initialize PIO and GPIO for parallel interface
int pico_parallel_init(void)
{
#ifdef PARALLEL_SENDER
    // Load the sender PIO program
    offset = pio_add_program(pio, &parallel_out_program);
    printf("PIO program loaded at offset %d\n", offset);

    // Configure GPIO pins for PIO (0-4)
    for (int i = 0; i < 4; i++)
    {
        pio_gpio_init(pio, i);
        gpio_set_drive_strength(16, GPIO_DRIVE_STRENGTH_12MA);
        printf("PIO GPIO %d initialized\n", i);
    }

    // Configure state machine
    pio_sm_config c = parallel_out_program_get_default_config(offset);
    sm_config_set_wrap(&c, offset + parallel_out_wrap_target, offset + parallel_out_wrap);
    sm_config_set_sideset(&c, 1, false, false);
    sm_config_set_out_pins(&c, 0, 3);  // 4 data pins starting at pin 0
    sm_config_set_sideset_pins(&c, 3); // Clock pin
    sm_config_set_out_shift(&c, true, true, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_NONE);
    // Set pin directions - only data pins, not the sideset clock pin
    pio_sm_set_consecutive_pindirs(pio, sm, 0, 4, true);
    // Set clock divider
    sm_config_set_clkdiv(&c, TX_CLOCK_DIV);

    // Initialize state machine
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);

    // Configure IRQ for sender
    pio_set_irq0_source_enabled(pio, pis_sm0_tx_fifo_not_full, true);
    // Configure IRQ handler
    irq_set_exclusive_handler(PIO0_IRQ_0, pio_irq_handler);
    irq_set_enabled(PIO0_IRQ_0, true);
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);
#else
    // Load the receiver PIO program
    offset = pio_add_program(pio, &parallel_in_program);

    // Configure GPIO pins
    for (int i = 0; i < 2; i++)
    {
        pio_gpio_init(pio, i);
    }
    pio_gpio_init(pio, 3); // Clock pin

    // Set pin directions - all pins as inputs

    // Configure state machine
    pio_sm_config c = parallel_in_program_get_default_config(offset);
    sm_config_set_wrap(&c, offset + parallel_in_wrap_target, offset + parallel_in_wrap);
    sm_config_set_in_pins(&c, 0);
    pio_sm_set_consecutive_pindirs(pio, sm, 0, 3, false);
    sm_config_set_in_shift(&c, true, true, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_NONE);
    sm_config_set_clkdiv(&c, TX_CLOCK_DIV / 4);

    // Add the board_filter PIO program
    uint filter_offset = pio_add_program(pio, &board_filter_program);

    // Configure board_filter SM
    pio_sm_config c_filter = board_filter_program_get_default_config(filter_offset);

    // Set the wrap point
    sm_config_set_wrap(&c_filter,
                       filter_offset + board_filter_wrap_target,
                       filter_offset + board_filter_wrap);

    // FIFO not joined (we push to RX FIFO, not auto-push)
    sm_config_set_out_shift(&c_filter, true, false, 32); // No auto-push
    sm_config_set_in_shift(&c_filter, true, false, 32);  // No auto-push
    sm_config_set_fifo_join(&c_filter, PIO_FIFO_JOIN_NONE);
    sm_config_set_clkdiv(&c_filter, 1);

    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);
    pio_sm_init(pio, sm_filter, filter_offset, &c_filter);
    pio_sm_set_enabled(pio, sm_filter, true);

    // Load the local board ID into OSR (to compare against incoming board_id)
    pio_sm_put_blocking(pio, sm_filter, BOARD_ID << 24); // 4-bit value: 0–15 shifted up to avoid a shift in PIO comparison

#endif

    // Initialize state machine

    return PICO_OK;
}

// Send data through the parallel interface (sender only)
#ifdef PARALLEL_SENDER
void pico_parallel_write(uint32_t data)
{
    pio_sm_put_blocking(pio, sm, data);
}
#endif

// Perform initialisation
int pico_led_init(void)
{
#if defined(PICO_DEFAULT_LED_PIN)
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    return PICO_OK;
#elif defined(CYW43_WL_GPIO_LED_PIN)
    return cyw43_arch_init();
#endif
}

// Turn the led on or off
void pico_set_led(bool led_on)
{
#if defined(PICO_DEFAULT_LED_PIN)
    gpio_put(PICO_DEFAULT_LED_PIN, led_on);
#elif defined(CYW43_WL_GPIO_LED_PIN)
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_on);
#endif
}
uint64_t interrupted_count = 0;

int main()
{
    stdio_init_all();
    // Overclock the Pico
    vreg_set_voltage(VREG_VOLTAGE_1_30);
    sleep_ms(500);
    setup_default_uart();
    set_sys_clock_khz(SYS_CLOCK_KHZ, true);
    setup_default_uart();
    // Increase system voltage to 1.2V
    printf("System clock set to 250MHz\n");

    // Initialize stdio

    // Reinitialize UART after clock change
    stdio_usb_init();
    setup_default_uart();

    // Wait for USB CDC to be ready
    printf("Waiting for USB CDC to be ready...\n");

    printf("USB CDC initialized\n");
    gpio_init(16);
    gpio_set_dir(16, GPIO_OUT);
    gpio_put(16, true);

    printf("GP16 configured as output and set high\n");
    int rc = pico_parallel_init();

    dma_channel_claim(DMA_CHANNEL0);
    dma_channel_claim(DMA_FILTER);

    // Configure DMA channel
    dma_config0 = dma_channel_get_default_config(DMA_CHANNEL0);
    channel_config_set_transfer_data_size(&dma_config0, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_config0, false);
    channel_config_set_write_increment(&dma_config0, false);
    channel_config_set_dreq(&dma_config0, DREQ_PIO0_RX0);
    // Set up DMA transfer
    dma_channel_configure(
        DMA_CHANNEL0,
        &dma_config0,
        &pio0_hw->txf[sm_filter], // Write address - start with buffer 0
        &pio0_hw->rxf[sm],        // Read address (PIO RX FIFO)
        BUFFER_SIZE,              // Number of transfers
        true                      // Start immediately
    );

    // Enable DMA interrupts
    dma_channel_set_irq0_enabled(DMA_CHANNEL0, true);

    // FILTER
    dma_config_filter = dma_channel_get_default_config(DMA_FILTER);
    channel_config_set_transfer_data_size(&dma_config_filter, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_config_filter, false);
    channel_config_set_write_increment(&dma_config_filter, true);
    channel_config_set_dreq(&dma_config_filter, DREQ_PIO0_RX1);
    // Set up DMA transfer

    dma_channel_configure(
        DMA_FILTER,
        &dma_config_filter,
        &received_data_buffer[0], // Write address - start with buffer 0
        &pio0_hw->rxf[sm_filter], // Read address (PIO RX FIFO)
        BUFFER_SIZE,              // Number of transfers
        true                      // Start immediately
    );
    dma_channel_set_irq1_enabled(DMA_FILTER, true);

    irq_set_exclusive_handler(DMA_IRQ, dma_irq_handler);

    irq_set_exclusive_handler(DMA_IRQ_FILTER, dma_irq_filter_handler);
    irq_set_enabled(DMA_IRQ, true);
    irq_set_enabled(DMA_IRQ_FILTER, true);

    volatile int a = 0;
    // Main loop - continuously feed data
    uint64_t last_time = 0;
    uint8_t board_id = 0;
    bool bad_data = false;
    while (true)
    {

        uint64_t current_time = time_us_64();
        if (current_time - last_time > 1000000)
        {

            printf("Mbps: %f\n", (float)counter * 32.0 / 1000000.0);
            printf("String: %s\n", string_buffer);
            printf("Successes: %d\n", successes);
            printf("Failures: %d\n", failures);
            printf("Success rate: %f\n", (float)successes / (float)(successes + failures));
            printf("Failure rate: %f\n", (float)failures / (float)(successes + failures));
            printf("DMA Transfers: %d\n", dma_transfers);

            printf("Interrupted: %d\n", interrupted);

            printf("Bypass: %d\n", bypass);
            // printf 32 bit unsigned integer
            printf("Value: 0x%08" PRIx32 "\n", sample);
            // print sample as 4 characters
            printf("Sample: %c%c%c%c\n", (sample >> 24) & 0xFF, (sample >> 16) & 0xFF, (sample >> 8) & 0xFF, sample & 0xFF);

            failures = 0;
            bypass = 0;
            successes = 0;
            dma_transfers = 0;
            last_time = current_time;
            counter = 0;
            interrupted = 0;
            interrupted_count = 0;
        }

        if (multicore_fifo_rvalid())

        {
            uint32_t buffer_to_process = multicore_fifo_pop_blocking();
            // sample = buffer_to_process;
            uint32_t last_board_id = 0;
            // Process the received data
            for (int i = 0; i < BUFFER_SIZE; i++)
            {
                if (multicore_fifo_rvalid())
                {

                    interrupted_count++;
                }
                uint32_t data = received_data_buffer[buffer_to_process * BUFFER_SIZE + i];

                uint header = data & 0xC0000000;

                if (header != PIXEL_HEADER)
                {
                    if (header == STRING_HEADER)
                    {

                        data_string_index = 0;
                    }
                    else
                    {
                        bypass++;
                    }
                    counter += 2;
                    continue;
                }
                // bypass++;
                counter++;

                uint32_t received_data = data & 0xFFFFFF;

                string_buffer[data_string_index] = (received_data >> 16) & 0xFF;
                string_buffer[data_string_index + 1] = (received_data >> 8) & 0xFF;
                string_buffer[data_string_index + 2] = received_data & 0xFF;
                data_string_index += 3;

                if (data_string_index >= data_string_len && data_string_index < 20)
                {

                    string_buffer[data_string_index] = '\0'; // Null terminate
                    data_string_index = 0;
                    if (strcmp(string_buffer, data_string) == 0)

                    {
                        successes++;
                    }
                    else
                    {
                        failures++;
                    }
                }
            }
            if (interrupted_count > 0)
            {
                interrupted++;
                interrupted_count = 0;
            }
        }
    }
}
