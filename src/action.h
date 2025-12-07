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

    // Sensor events
    ACTION_BOARD_ADDRESS_UPDATED,
    ACTION_SD_CARD_STATUS,

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

    // For ACTION_SD_CARD_STATUS
    struct {
        bool mounted;
        char message[64];
    } sd_card;

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

static inline Action action_sd_card_status(uint32_t timestamp, bool mounted, const char* msg) {
    Action a = {
        .type = ACTION_SD_CARD_STATUS,
        .timestamp = timestamp,
        .payload.sd_card.mounted = mounted,
    };
    // Safe copy
    for(int i=0; i<63; i++) {
        a.payload.sd_card.message[i] = msg[i];
        if(msg[i] == 0) break;
    }
    a.payload.sd_card.message[63] = 0;
    return a;
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
