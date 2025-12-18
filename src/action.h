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
    ACTION_SD_CARD_MOUNTED,
    ACTION_SD_CARD_ERROR,
    ACTION_SD_FILES_LOADED,

    // Animation events
    ACTION_RAINBOW_FRAME_COMPLETE,

    // Power events
    ACTION_POWER_TOGGLE,

    // Playback events
    ACTION_FSEQ_NEXT,           // Skip to next file during playback
    ACTION_AUTO_TOGGLE,         // Toggle auto-loop mode
    ACTION_FSEQ_LOOP_COMPLETE,  // Current file finished one loop (for auto-advance)

    // Brightness events
    ACTION_BRIGHTNESS_UP,
    ACTION_BRIGHTNESS_DOWN,
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

    // For ACTION_SD_CARD_ERROR
    struct {
        char message[24];
    } sd_error;

    // For ACTION_SD_FILES_LOADED
    struct {
        uint8_t count;  // Files stored in sd_file_list static buffer
    } sd_files;

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

static inline Action action_sd_card_mounted(uint32_t timestamp) {
    return (Action){
        .type = ACTION_SD_CARD_MOUNTED,
        .timestamp = timestamp,
    };
}

static inline Action action_sd_card_error(uint32_t timestamp, const char* msg) {
    Action a = {
        .type = ACTION_SD_CARD_ERROR,
        .timestamp = timestamp,
    };
    for(int i=0; i<23 && msg[i]; i++) {
        a.payload.sd_error.message[i] = msg[i];
    }
    a.payload.sd_error.message[23] = 0;
    return a;
}

static inline Action action_sd_files_loaded(uint32_t timestamp, uint8_t count) {
    return (Action){
        .type = ACTION_SD_FILES_LOADED,
        .timestamp = timestamp,
        .payload.sd_files.count = count,
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

static inline Action action_power_toggle(uint32_t timestamp) {
    return (Action){
        .type = ACTION_POWER_TOGGLE,
        .timestamp = timestamp,
    };
}

static inline Action action_fseq_next(uint32_t timestamp) {
    return (Action){
        .type = ACTION_FSEQ_NEXT,
        .timestamp = timestamp,
    };
}

static inline Action action_auto_toggle(uint32_t timestamp) {
    return (Action){
        .type = ACTION_AUTO_TOGGLE,
        .timestamp = timestamp,
    };
}

static inline Action action_fseq_loop_complete(uint32_t timestamp) {
    return (Action){
        .type = ACTION_FSEQ_LOOP_COMPLETE,
        .timestamp = timestamp,
    };
}

static inline Action action_brightness_up(uint32_t timestamp) {
    return (Action){
        .type = ACTION_BRIGHTNESS_UP,
        .timestamp = timestamp,
    };
}

static inline Action action_brightness_down(uint32_t timestamp) {
    return (Action){
        .type = ACTION_BRIGHTNESS_DOWN,
        .timestamp = timestamp,
    };
}
