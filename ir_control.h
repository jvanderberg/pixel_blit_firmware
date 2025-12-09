#ifndef IR_CONTROL_H
#define IR_CONTROL_H

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "pico/util/queue.h"

// IR Remote Button Codes (NEC protocol)
#define IR_PIN 42 // GPIO pin for IR receiver on pixel_blit

#define BRIGHTNESS_UP 0x3A
#define BRIGHTNESS_DN 0xBA
#define PLAY 0x82
#define POWER 0x02

#define RED1 0x1A
#define GREEN1 0x9A
#define BLUE1 0xA2
#define WHITE1 0x22

#define RED2 0x2A
#define GREEN2 0xAA
#define BLUE2 0x92
#define WHITE2 0x12

#define RED3 0x0A
#define GREEN3 0x8A
#define BLUE3 0xB2
#define WHITE3 0x32

#define RED4 0x38
#define GREEN4 0xB8
#define BLUE4 0x78
#define WHITE4 0xF8

#define RED5 0x18
#define GREEN5 0x98
#define BLUE5 0x58
#define WHITE5 0xD8

#define RED_UP 0x28
#define GREEN_UP 0xA8
#define BLUE_UP 0x68
#define QUICK 0xE8

#define RED_DN 0x08
#define GREEN_DN 0x88
#define BLUE_DN 0x48
#define SLOW 0xC8

#define DIY1 0x30
#define DIY2 0xB0
#define DIY3 0x70
#define AUTO 0xF0

#define DIY4 0x10
#define DIY5 0x90
#define DIY6 0x50
#define FLASH 0xD0

#define JUMP3 0x20
#define JUMP7 0xA0
#define FADE3 0x60
#define FADE7 0xE0

// Function declarations
void ir_init(uint gpio_pin);
void ir_process_edge(void);  // Call from combined GPIO ISR
bool ir_get_next_command(uint8_t *code);

#endif // IR_CONTROL_H
