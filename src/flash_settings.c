#include "flash_settings.h"
#include "core1_task.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "pico/flash.h"  // For flash_safe_execute
#include <string.h>

// Flash location: last 4KB sector
// PICO_FLASH_SIZE_BYTES is defined by the board config
#define FLASH_SETTINGS_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define FLASH_SETTINGS_ADDR   (XIP_BASE + FLASH_SETTINGS_OFFSET)

// Debounce settings saves (2 seconds - balance between flash wear and responsiveness)
#define SAVE_DEBOUNCE_US 2000000

// State for debounced saving
static flash_settings_t last_saved = {0};
static flash_settings_t pending_save = {0};
static bool save_pending = false;
static uint64_t save_pending_time = 0;
static bool initialized = false;

// Data passed to flash_safe_execute callback
typedef struct {
    const flash_settings_t* settings;
    uint8_t page_buffer[FLASH_PAGE_SIZE];
} flash_write_params_t;

// Simple CRC32 (polynomial 0xEDB88320)
static uint32_t crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

static uint32_t calc_settings_crc(const flash_settings_t* settings) {
    // CRC covers everything except the CRC field itself
    return crc32((const uint8_t*)settings, offsetof(flash_settings_t, crc));
}

bool flash_settings_load(flash_settings_t* settings) {
    if (!settings) return false;

    // Read directly from flash (memory-mapped via XIP)
    const flash_settings_t* flash_data = (const flash_settings_t*)FLASH_SETTINGS_ADDR;

    // Check magic number
    if (flash_data->magic != FLASH_SETTINGS_MAGIC) return false;

    // Check version - support migration from v1
    if (flash_data->version != FLASH_SETTINGS_VERSION) {
        if (flash_data->version == 1) {
            // Migrate from v1: auto_loop defaults to false
            // CRC check uses v1 layout, so skip CRC for migration
            if (flash_data->brightness < 1 || flash_data->brightness > 10) return false;
            if (flash_data->playing_index > 15) return false;
            settings->magic = FLASH_SETTINGS_MAGIC;
            settings->version = FLASH_SETTINGS_VERSION;
            settings->brightness = flash_data->brightness;
            settings->was_playing = flash_data->was_playing;
            settings->playing_index = flash_data->playing_index;
            settings->auto_loop = 0;  // Default for migrated settings
            memset(settings->reserved, 0, sizeof(settings->reserved));
            settings->crc = calc_settings_crc(settings);
            return true;
        }
        return false;  // Unknown version
    }

    // Verify CRC
    uint32_t expected_crc = calc_settings_crc(flash_data);
    if (flash_data->crc != expected_crc) return false;

    // Validate ranges
    if (flash_data->brightness < 1 || flash_data->brightness > 10) return false;
    if (flash_data->playing_index > 15) return false;

    // Copy to output
    memcpy(settings, flash_data, sizeof(flash_settings_t));

    // DON'T set last_saved or initialized here!
    // Let check_save() initialize last_saved from the actual current state on first call.
    // This prevents false "change" detection when is_playing starts as false
    // but we loaded was_playing=true (auto-play hasn't triggered yet).

    return true;
}

// Direct flash write - only safe when core1 is not running
static void do_flash_write_direct(const uint8_t* page_buffer) {
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_SETTINGS_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_SETTINGS_OFFSET, page_buffer, FLASH_PAGE_SIZE);
    restore_interrupts(ints);
}

// Callback for flash_safe_execute - runs with other core paused
static void do_flash_write_callback(void* param) {
    flash_write_params_t* p = (flash_write_params_t*)param;

    // Erase the sector (required before writing)
    flash_range_erase(FLASH_SETTINGS_OFFSET, FLASH_SECTOR_SIZE);

    // Write the settings
    flash_range_program(FLASH_SETTINGS_OFFSET, p->page_buffer, FLASH_PAGE_SIZE);
}

void flash_settings_save(const flash_settings_t* settings) {
    if (!settings) return;

    // Prepare data with CRC
    flash_settings_t to_write;
    memcpy(&to_write, settings, sizeof(flash_settings_t));
    to_write.magic = FLASH_SETTINGS_MAGIC;
    to_write.version = FLASH_SETTINGS_VERSION;
    to_write.crc = calc_settings_crc(&to_write);

    // Prepare page buffer
    uint8_t page_buffer[FLASH_PAGE_SIZE];
    memset(page_buffer, 0xFF, FLASH_PAGE_SIZE);
    memcpy(page_buffer, &to_write, sizeof(flash_settings_t));

    // Check if core1 is running a task
    bool core1_active = !core1_is_idle();

    if (core1_active) {
        // Core1 is running - need to use flash_safe_execute to pause it
        flash_write_params_t params;
        params.settings = &to_write;
        memcpy(params.page_buffer, page_buffer, FLASH_PAGE_SIZE);

        int rc = flash_safe_execute(do_flash_write_callback, &params, UINT32_MAX);
        if (rc != PICO_OK) return;
    } else {
        // Core1 not running - safe to write directly
        do_flash_write_direct(page_buffer);
    }

    // Update last saved state
    memcpy(&last_saved, &to_write, sizeof(flash_settings_t));
    save_pending = false;
    initialized = true;
}

// Callback for flash erase
static void do_flash_erase_callback(void* param) {
    (void)param;
    flash_range_erase(FLASH_SETTINGS_OFFSET, FLASH_SECTOR_SIZE);
}

// Direct flash erase - only safe when core1 is not running
static void do_flash_erase_direct(void) {
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_SETTINGS_OFFSET, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
}

void flash_settings_clear(void) {
    bool core1_active = !core1_is_idle();

    if (core1_active) {
        int rc = flash_safe_execute(do_flash_erase_callback, NULL, UINT32_MAX);
        if (rc != PICO_OK) return;
    } else {
        do_flash_erase_direct();
    }
    memset(&last_saved, 0, sizeof(flash_settings_t));
    save_pending = false;
}

void flash_settings_check_save(uint8_t brightness, bool is_playing, uint8_t playing_index, bool auto_loop) {
    // Initialize last_saved on first call if not loaded from flash
    if (!initialized) {
        last_saved.brightness = brightness;
        last_saved.was_playing = is_playing ? 1 : 0;
        last_saved.playing_index = playing_index;
        last_saved.auto_loop = auto_loop ? 1 : 0;
        initialized = true;
        return;
    }

    // Check if settings changed
    bool changed = (brightness != last_saved.brightness) ||
                   ((is_playing ? 1 : 0) != last_saved.was_playing) ||
                   (playing_index != last_saved.playing_index) ||
                   ((auto_loop ? 1 : 0) != last_saved.auto_loop);

    if (changed) {
        // Update pending save data
        pending_save.brightness = brightness;
        pending_save.was_playing = is_playing ? 1 : 0;
        pending_save.playing_index = playing_index;
        pending_save.auto_loop = auto_loop ? 1 : 0;

        // Update last_saved so we don't detect the same change next frame
        last_saved.brightness = brightness;
        last_saved.was_playing = is_playing ? 1 : 0;
        last_saved.playing_index = playing_index;
        last_saved.auto_loop = auto_loop ? 1 : 0;

        // Reset debounce timer
        save_pending = true;
        save_pending_time = time_us_64() + SAVE_DEBOUNCE_US;
    }

    // Check if debounce period has elapsed
    if (save_pending && time_us_64() >= save_pending_time) {
        flash_settings_save(&pending_save);
    }
}
