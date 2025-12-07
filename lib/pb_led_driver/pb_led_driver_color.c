/**
 * pb_led_driver_color.c - Color utility implementations
 */

#include "pb_led_driver.h"

pb_color_t pb_color_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

pb_color_t pb_color_hsv(uint8_t h, uint8_t s, uint8_t v) {
    // Fast integer HSV to RGB conversion
    // h: 0-255 (hue, wraps around)
    // s: 0-255 (saturation)
    // v: 0-255 (value/brightness)

    if (s == 0) {
        // Grayscale
        return pb_color_rgb(v, v, v);
    }

    // Scale h to 0-1535 (6 * 256) for 6 sectors
    uint16_t h6 = (uint16_t)h * 6;
    uint8_t sector = h6 >> 8;           // 0-5
    uint8_t frac = h6 & 0xFF;           // Fractional part within sector

    // Calculate intermediate values
    uint8_t p = (uint8_t)((uint16_t)v * (255 - s) / 255);
    uint8_t q = (uint8_t)((uint16_t)v * (255 - ((uint16_t)s * frac / 255)) / 255);
    uint8_t t = (uint8_t)((uint16_t)v * (255 - ((uint16_t)s * (255 - frac) / 255)) / 255);

    switch (sector) {
        case 0:  return pb_color_rgb(v, t, p);  // Red to Yellow
        case 1:  return pb_color_rgb(q, v, p);  // Yellow to Green
        case 2:  return pb_color_rgb(p, v, t);  // Green to Cyan
        case 3:  return pb_color_rgb(p, q, v);  // Cyan to Blue
        case 4:  return pb_color_rgb(t, p, v);  // Blue to Magenta
        default: return pb_color_rgb(v, p, q);  // Magenta to Red
    }
}

pb_color_t pb_color_scale(pb_color_t color, uint8_t scale) {
    uint8_t r = (uint8_t)(((color >> 16) & 0xFF) * scale / 255);
    uint8_t g = (uint8_t)(((color >> 8) & 0xFF) * scale / 255);
    uint8_t b = (uint8_t)((color & 0xFF) * scale / 255);
    return pb_color_rgb(r, g, b);
}

pb_color_t pb_color_blend(pb_color_t c1, pb_color_t c2, uint8_t amount) {
    // amount: 0 = all c1, 255 = all c2
    uint8_t inv = 255 - amount;
    uint8_t r = (uint8_t)((((c1 >> 16) & 0xFF) * inv + ((c2 >> 16) & 0xFF) * amount) / 255);
    uint8_t g = (uint8_t)((((c1 >> 8) & 0xFF) * inv + ((c2 >> 8) & 0xFF) * amount) / 255);
    uint8_t b = (uint8_t)(((c1 & 0xFF) * inv + (c2 & 0xFF) * amount) / 255);
    return pb_color_rgb(r, g, b);
}
