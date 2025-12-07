#include "string_test.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "string_test.pio.h"

#define STRING_TEST_UPDATE_INTERVAL_US 2000

static const uint8_t freq_steps[32] = {
    2, 3, 4, 5, 6, 7, 8, 9,
    10, 11, 12, 13, 14, 15, 16, 17,
    18, 19, 20, 21, 22, 23, 24, 25,
    26, 27, 28, 29, 30, 31, 32, 33};

static inline void init_counters(string_test_t *ctx)
{
    for (int i = 0; i < 32; i++)
    {
        ctx->counters[i] = freq_steps[i];
    }
}

bool string_test_init(string_test_t *ctx, uint first_pin)
{
    if (!ctx)
    {
        return false;
    }

    ctx->pio = pio0;
    ctx->first_pin = first_pin;
    ctx->offset = pio_add_program(ctx->pio, &string_test_program);
    ctx->sm = pio_claim_unused_sm(ctx->pio, true);

    for (uint pin = first_pin; pin < first_pin + 32; ++pin)
    {
        pio_gpio_init(ctx->pio, pin);
    }

    pio_sm_config c = string_test_program_get_default_config(ctx->offset);
    sm_config_set_out_pins(&c, first_pin, 32);
    sm_config_set_set_pins(&c, first_pin, 32);
    sm_config_set_out_shift(&c, false, true, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_NONE);
    sm_config_set_clkdiv(&c, 1000.0f);
    pio_sm_init(ctx->pio, ctx->sm, ctx->offset, &c);
    pio_sm_set_consecutive_pindirs(ctx->pio, ctx->sm, first_pin, 32, true);
    pio_sm_set_enabled(ctx->pio, ctx->sm, false);

    ctx->running = false;
    ctx->output_state = 0;
    init_counters(ctx);
    ctx->next_update = get_absolute_time();
    return true;
}

void string_test_start(string_test_t *ctx)
{
    if (!ctx || ctx->running)
    {
        return;
    }
    for (uint pin = ctx->first_pin; pin < ctx->first_pin + 32; ++pin)
    {
        pio_gpio_init(ctx->pio, pin);
    }
    pio_sm_set_consecutive_pindirs(ctx->pio, ctx->sm, ctx->first_pin, 32, true);

    ctx->output_state = 0;
    init_counters(ctx);
    ctx->next_update = get_absolute_time();
    pio_sm_clear_fifos(ctx->pio, ctx->sm);
    pio_sm_restart(ctx->pio, ctx->sm);
    pio_sm_put_blocking(ctx->pio, ctx->sm, ctx->output_state);
    pio_sm_set_enabled(ctx->pio, ctx->sm, true);
    ctx->running = true;
}

void string_test_stop(string_test_t *ctx)
{
    if (!ctx)
    {
        return;
    }
    if (ctx->running)
    {
        ctx->running = false;
        ctx->output_state = 0;
        pio_sm_put_blocking(ctx->pio, ctx->sm, ctx->output_state);
    }
    pio_sm_set_enabled(ctx->pio, ctx->sm, false);
}

bool string_test_is_running(const string_test_t *ctx)
{
    return ctx && ctx->running;
}

void string_test_task(string_test_t *ctx)
{
    if (!ctx || !ctx->running)
    {
        return;
    }

    if (!time_reached(ctx->next_update))
    {
        return;
    }
    ctx->next_update = delayed_by_us(ctx->next_update, STRING_TEST_UPDATE_INTERVAL_US);

    for (int i = 0; i < 32; i++)
    {
        if (--ctx->counters[i] == 0)
        {
            ctx->counters[i] = freq_steps[i];
            ctx->output_state ^= (1u << i);
        }
    }

    pio_sm_put_blocking(ctx->pio, ctx->sm, ctx->output_state);
}
