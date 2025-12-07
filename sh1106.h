#ifndef SH1106_H
#define SH1106_H

#include "hardware/i2c.h"
#include <stdbool.h>
#include <stdint.h>

#define SH1106_WIDTH 128
#define SH1106_HEIGHT 64

typedef struct
{
    i2c_inst_t *i2c;
    uint8_t addr;
    uint8_t buffer[SH1106_WIDTH * SH1106_HEIGHT / 8];
} sh1106_t;

bool sh1106_init(sh1106_t *dev, i2c_inst_t *i2c, uint8_t addr);
void sh1106_clear(sh1106_t *dev);
void sh1106_draw_string(sh1106_t *dev, int x, int y, const char *text, bool invert);
void sh1106_render(sh1106_t *dev);

#endif // SH1106_H
