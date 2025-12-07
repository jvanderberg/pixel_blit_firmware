#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"

#define DIP_ADC_GPIO 47 // RP2350 GPIO 47 = ADC7
#define DIP_ADC_CH 7    // ADC channel for GPIO 47

// Ideal 12-bit codes @ 3.3V for pull-up 47k; legs 47k (B0), 100k (B1), 220k (B2), 470k (B3).
// Order is **sorted high→low voltage** (not numeric).
static const uint16_t level_codes[16] = {
    4095, // 0000
    3723, // 1000
    3374, // 0100
    3117, // 1100
    2786, // 0010
    2608, // 1010
    2432, // 0110
    2296, // 1110
    2048, // 0001  <-- your 47k branch ON alone
    1950, // 1001
    1850, // 0101
    1770, // 1101
    1658, // 0011
    1593, // 1011
    1526, // 0111
    1471  // 1111
};

// Mapping from that sorted order back to the 4-bit code (B3..B0).
static const uint8_t code_by_rank[16] = {
    0x0, 0x8, 0x4, 0xC, 0x2, 0xA, 0x6, 0xE, 0x1, 0x9, 0x5, 0xD, 0x3, 0xB, 0x7, 0xF};

static inline uint16_t adc_read_avg_12b(int ch, int nsamples)
{
    adc_select_input(ch);
    uint32_t sum = 0;

    for (int i = 0; i < nsamples; i++)
    {

        sum += adc_read();
        sleep_us(100);
    }
    return (uint16_t)(sum / (nsamples));
}

// Call once at boot before reading
void dip_adc_init(void)
{
    adc_init();
    // Detach digital buffer and set up pad for analog
    adc_gpio_init(DIP_ADC_GPIO); // puts GPIO into analog mode (hi-Z to your ladder)
    // Optional: small settle (in case you just switched modes)
    sleep_ms(2);
}

// Returns 0..15 from the DIP ladder by nearest-neighbor decode
uint8_t dip_read_addr(void)
{
    int next_best_err = 0;
    const int N = 100;
    uint16_t v = adc_read_avg_12b(DIP_ADC_CH, N);
    printf("ADC read: %u\n", v);

    // Find nearest rank
    int best_i = 0;
    uint16_t best_err = (level_codes[0] > v) ? (level_codes[0] - v) : (v - level_codes[0]);
    for (int i = 1; i < 16; i++)
    {

        uint16_t err = (level_codes[i] > v) ? (level_codes[i] - v) : (v - level_codes[i]);
        if (err < best_err)
        {

            best_err = err;

            best_i = i;
        }
        if (err > best_err && (next_best_err == 0 || err < next_best_err))
        {
            next_best_err = err;
        }
        // printf("Code %u: level %u, err %u\n, best: %u\n", i, level_codes[i], err, best_i);
    }

    printf("Code %u: level %u, berr %u, nberr: %u, best: %u\n", best_i, code_by_rank[best_i], best_err, next_best_err, best_i);
    printf("margin: %d\n", next_best_err - best_err);
    uint8_t raw_code = code_by_rank[best_i];
    return raw_code;
}
int main()
{
    stdio_init_all();
    dip_adc_init();
    while (true)
    {
        printf("Hello, world!\n");
        sleep_ms(1000);
        uint8_t addr = dip_read_addr();
        printf("DIP raw code: 0x%X\n", addr);
    }
}
