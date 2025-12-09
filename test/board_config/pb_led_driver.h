/**
 * pb_led_driver.h - Minimal stub for host testing
 * Only includes the color order enum needed by board_config
 */

#ifndef PB_LED_DRIVER_H
#define PB_LED_DRIVER_H

#include <stdint.h>

/** LED color order */
typedef enum {
    PB_COLOR_ORDER_GRB = 0,  // WS2812
    PB_COLOR_ORDER_RGB,      // WS2811
    PB_COLOR_ORDER_BGR,
    PB_COLOR_ORDER_RBG,
    PB_COLOR_ORDER_GBR,
    PB_COLOR_ORDER_BRG,
} pb_color_order_t;

#endif // PB_LED_DRIVER_H
