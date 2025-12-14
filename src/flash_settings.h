#pragma once

#include <stdint.h>
#include <stdbool.h>

// Settings structure persisted to flash
typedef struct {
    uint32_t magic;           // 0x50425345 ("PBSE" - PixelBlit Settings)
    uint8_t version;          // Schema version (for future migration)
    uint8_t brightness;       // 1-10
    uint8_t was_playing;      // bool: was FSEQ playback active?
    uint8_t playing_index;    // 0-15: which file was playing
    uint32_t crc;             // CRC32 of fields above
} flash_settings_t;

#define FLASH_SETTINGS_MAGIC 0x50425345  // "PBSE"
#define FLASH_SETTINGS_VERSION 1

// Load settings from flash
// Returns true if valid settings were loaded, false if defaults should be used
bool flash_settings_load(flash_settings_t* settings);

// Save settings to flash (handles XIP safety internally)
// Note: This erases a 4KB sector and writes - use sparingly
void flash_settings_save(const flash_settings_t* settings);

// Clear saved settings (revert to defaults on next boot)
void flash_settings_clear(void);

// Check if settings need saving (call periodically from main loop)
// Implements debouncing to avoid excessive flash writes
void flash_settings_check_save(uint8_t brightness, bool is_playing, uint8_t playing_index);
