#pragma once

#include <stdint.h>

// Action types - represent "what happened"
typedef enum {
    ACTION_NONE = 0,

    // Button events
    ACTION_BUTTON_SELECT,
    ACTION_BUTTON_NEXT,

    // Timer events
    ACTION_TICK_1S,          // 1 second tick for uptime, logging
    ACTION_TICK_FAST,        // Fast tick for animations

    // Sensor events
    ACTION_BOARD_ADDRESS_UPDATED,

    // Animation events
    ACTION_RAINBOW_FRAME_COMPLETE,
} ActionType;

// Action payload union
typedef union {
    // For ACTION_BOARD_ADDRESS_UPDATED
    struct {
        uint16_t adc_value;
        uint8_t code;
        uint16_t error;
        uint16_t margin;
    } board_address;

    // For ACTION_RAINBOW_FRAME_COMPLETE
    struct {
        uint16_t fps;
    } rainbow;
} ActionPayload;

// Action struct
typedef struct {
    ActionType type;
    uint32_t timestamp;
    ActionPayload payload;
} Action;

// Factory functions for creating actions
static inline Action action_none(void) {
    return (Action){.type = ACTION_NONE};
}

static inline Action action_button_select(uint32_t timestamp) {
    return (Action){
        .type = ACTION_BUTTON_SELECT,
        .timestamp = timestamp,
    };
}

static inline Action action_button_next(uint32_t timestamp) {
    return (Action){
        .type = ACTION_BUTTON_NEXT,
        .timestamp = timestamp,
    };
}

static inline Action action_tick_1s(uint32_t timestamp) {
    return (Action){
        .type = ACTION_TICK_1S,
        .timestamp = timestamp,
    };
}

static inline Action action_tick_fast(uint32_t timestamp) {
    return (Action){
        .type = ACTION_TICK_FAST,
        .timestamp = timestamp,
    };
}

static inline Action action_board_address_updated(uint32_t timestamp,
                                                   uint16_t adc_value,
                                                   uint8_t code,
                                                   uint16_t error,
                                                   uint16_t margin) {
    return (Action){
        .type = ACTION_BOARD_ADDRESS_UPDATED,
        .timestamp = timestamp,
        .payload.board_address = {
            .adc_value = adc_value,
            .code = code,
            .error = error,
            .margin = margin,
        },
    };
}

static inline Action action_rainbow_frame_complete(uint32_t timestamp, uint16_t fps) {
    return (Action){
        .type = ACTION_RAINBOW_FRAME_COMPLETE,
        .timestamp = timestamp,
        .payload.rainbow = {
            .fps = fps,
        },
    };
}
