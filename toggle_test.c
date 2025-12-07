#include "toggle_test.h"

#define TOGGLE_HALF_PERIOD_US 500000 // 0.5s high, 0.5s low

static void apply_level(toggle_test_t *ctx)
{
    gpio_put_masked(ctx->mask, ctx->level_high ? ctx->mask : 0);
}

bool toggle_test_init(toggle_test_t *ctx, uint base_pin)
{
    if (!ctx)
    {
        return false;
    }
    ctx->base_pin = base_pin;
    ctx->mask = 0;
    for (uint pin = base_pin; pin < base_pin + 32; ++pin)
    {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_OUT);
        gpio_put(pin, 0);
        ctx->mask |= (1u << pin);
    }
    ctx->running = false;
    ctx->level_high = false;
    ctx->next_toggle = get_absolute_time();
    return true;
}

void toggle_test_start(toggle_test_t *ctx)
{
    if (!ctx || ctx->running)
    {
        return;
    }
    for (uint pin = ctx->base_pin; pin < ctx->base_pin + 32; ++pin)
    {
        gpio_set_function(pin, GPIO_FUNC_SIO);
        gpio_set_dir(pin, GPIO_OUT);
    }
    ctx->level_high = false;
    apply_level(ctx);
    ctx->next_toggle = delayed_by_us(get_absolute_time(), TOGGLE_HALF_PERIOD_US);
    ctx->running = true;
}

void toggle_test_stop(toggle_test_t *ctx)
{
    if (!ctx)
    {
        return;
    }
    ctx->running = false;
    ctx->level_high = false;
    apply_level(ctx);
}

bool toggle_test_is_running(const toggle_test_t *ctx)
{
    return ctx && ctx->running;
}

void toggle_test_task(toggle_test_t *ctx)
{
    if (!ctx || !ctx->running)
    {
        return;
    }
    absolute_time_t now = get_absolute_time();
    if (absolute_time_diff_us(now, ctx->next_toggle) > 0)
    {
        return;
    }
    ctx->level_high = !ctx->level_high;
    apply_level(ctx);
    ctx->next_toggle = delayed_by_us(ctx->next_toggle, TOGGLE_HALF_PERIOD_US);
}
