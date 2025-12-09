/**
 * pb_led_driver_hw.c - PIO and DMA hardware implementation
 *
 * This file contains all hardware-specific code for RP2350B.
 * Only compiled when building for Pico SDK (not for host tests).
 */

#ifndef PB_LED_DRIVER_TEST_BUILD

#include "pb_led_driver.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include "pico/sem.h"
#include "pico/time.h"
#include <string.h>

// Generated PIO header
#include "ws2811_parallel.pio.h"

// ============================================================================
// DMA Configuration
// ============================================================================

#define PB_DMA_CHANNEL 8  // Use channel 8 to avoid conflict with SD card

// ============================================================================
// Hardware state (internal)
// ============================================================================

typedef struct {
    PIO pio;
    uint sm;
    uint pio_offset;

    // Transfer size: pixels * 3 channels * 8 planes
    uint32_t transfer_words;

    // Reset delay from config
    uint16_t reset_us;

    // Synchronization
    struct semaphore reset_delay_sem;
    alarm_id_t reset_alarm_id;

    bool initialized;
} pb_hw_state_t;

static pb_hw_state_t hw_state = {0};

// Forward declarations
extern pb_value_bits_t* pb_driver_get_front_buffer(pb_driver_t* driver);
extern void pb_driver_swap_buffers(pb_driver_t* driver);

// ============================================================================
// Interrupt handlers
// ============================================================================

static int64_t reset_delay_complete(alarm_id_t id, void* user_data) {
    (void)id;
    (void)user_data;
    hw_state.reset_alarm_id = 0;
    sem_release(&hw_state.reset_delay_sem);
    return 0;  // Don't reschedule
}

static void __isr dma_complete_handler(void) {
    if (dma_hw->ints0 & (1u << PB_DMA_CHANNEL)) {
        // Clear interrupt
        dma_hw->ints0 = (1u << PB_DMA_CHANNEL);

        // Cancel any pending alarm and start reset delay
        if (hw_state.reset_alarm_id) {
            cancel_alarm(hw_state.reset_alarm_id);
        }
        // WS2811/WS2812 reset delay from config (typically 200us)
        hw_state.reset_alarm_id = add_alarm_in_us(hw_state.reset_us, reset_delay_complete, NULL, true);
    }
}

// ============================================================================
// Hardware initialization
// ============================================================================

int pb_hw_init(pb_driver_t* driver) {
    if (hw_state.initialized) {
        return -1;  // Already initialized
    }

    const pb_driver_config_t* config = pb_driver_get_config(driver);
    if (config == NULL) {
        return -1;
    }

    // Initialize semaphore (start with 0 - first show will wait for reset)
    sem_init(&hw_state.reset_delay_sem, 0, 1);
    hw_state.reset_alarm_id = 0;

    // Schedule initial reset delay so first frame waits for LEDs to be ready
    hw_state.reset_alarm_id = add_alarm_in_us(300, reset_delay_complete, NULL, true);

    // Calculate transfer size: pixels * 3 channels * 8 bit-planes
    hw_state.transfer_words = config->max_pixel_length * 3 * 8;

    // Store reset delay (use 200us minimum if not specified)
    hw_state.reset_us = config->reset_us > 0 ? config->reset_us : 200;

    // Claim PIO and load program
    hw_state.pio = (config->pio_index == 0) ? pio0 : pio1;

    bool success = pio_claim_free_sm_and_add_program_for_gpio_range(
        &ws2811_parallel_program,
        &hw_state.pio,
        &hw_state.sm,
        &hw_state.pio_offset,
        config->gpio_base,
        config->num_strings,
        true  // Required
    );
    if (!success) {
        return -2;  // Failed to claim PIO resources
    }

    // Initialize PIO state machine
    ws2811_parallel_program_init(
        hw_state.pio,
        hw_state.sm,
        hw_state.pio_offset,
        config->gpio_base,
        config->num_strings,
        (float)config->frequency_hz
    );

    // Setup single DMA channel
    dma_channel_claim(PB_DMA_CHANNEL);

    dma_channel_config cfg = dma_channel_get_default_config(PB_DMA_CHANNEL);
    channel_config_set_dreq(&cfg, pio_get_dreq(hw_state.pio, hw_state.sm, true));
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&cfg, true);
    channel_config_set_write_increment(&cfg, false);

    dma_channel_configure(
        PB_DMA_CHANNEL,
        &cfg,
        &hw_state.pio->txf[hw_state.sm],  // Write to PIO TX FIFO
        NULL,                              // Read address set at show time
        hw_state.transfer_words,           // Total words to transfer
        false                              // Don't start yet
    );

    // Setup interrupt handler
    irq_set_exclusive_handler(DMA_IRQ_0, dma_complete_handler);
    dma_channel_set_irq0_enabled(PB_DMA_CHANNEL, true);
    irq_set_enabled(DMA_IRQ_0, true);

    hw_state.initialized = true;
    return 0;
}

void pb_hw_deinit(void) {
    if (!hw_state.initialized) {
        return;
    }

    // Disable interrupts
    irq_set_enabled(DMA_IRQ_0, false);
    dma_channel_set_irq0_enabled(PB_DMA_CHANNEL, false);

    // Cancel any pending alarm
    if (hw_state.reset_alarm_id) {
        cancel_alarm(hw_state.reset_alarm_id);
        hw_state.reset_alarm_id = 0;
    }

    // Release DMA channel
    dma_channel_unclaim(PB_DMA_CHANNEL);

    // Release PIO resources
    pio_remove_program_and_unclaim_sm(&ws2811_parallel_program,
                                       hw_state.pio, hw_state.sm,
                                       hw_state.pio_offset);

    hw_state.initialized = false;
}

// ============================================================================
// Show implementation
// ============================================================================

bool pb_hw_show(pb_driver_t* driver, bool blocking) {
    if (!hw_state.initialized) {
        return false;
    }

    // Wait for previous transfer to complete (reset delay)
    if (blocking) {
        sem_acquire_blocking(&hw_state.reset_delay_sem);
    } else {
        if (!sem_try_acquire(&hw_state.reset_delay_sem)) {
            return false;  // Previous transfer still in progress
        }
    }

    // Swap buffers NOW - after semaphore acquired, previous DMA is done
    // This ensures the old front buffer is safe to become the new back buffer
    pb_driver_swap_buffers(driver);

    // Get the new front buffer (the one we just finished writing to)
    pb_value_bits_t* buffer = pb_driver_get_front_buffer(driver);
    if (buffer == NULL) {
        sem_release(&hw_state.reset_delay_sem);
        return false;
    }

    // Start DMA from contiguous buffer
    dma_channel_set_read_addr(PB_DMA_CHANNEL, buffer, false);
    dma_channel_set_trans_count(PB_DMA_CHANNEL, hw_state.transfer_words, true);
    return true;
}

bool pb_hw_show_busy(void) {
    if (!hw_state.initialized) {
        return false;
    }
    // Check if we can acquire the semaphore without blocking
    if (sem_try_acquire(&hw_state.reset_delay_sem)) {
        sem_release(&hw_state.reset_delay_sem);
        return false;  // Not busy
    }
    return true;  // Busy
}

void pb_hw_show_wait(void) {
    if (!hw_state.initialized) {
        return;
    }
    sem_acquire_blocking(&hw_state.reset_delay_sem);
    sem_release(&hw_state.reset_delay_sem);
}

#endif // !PB_LED_DRIVER_TEST_BUILD
