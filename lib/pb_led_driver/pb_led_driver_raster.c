/**
 * pb_led_driver_raster.c - Raster abstraction layer
 *
 * Provides a 2D pixel buffer with precomputed mapping to physical LED strings.
 * Mapping is built once at create time based on wrap mode.
 */

#include "pb_led_driver.h"
#include <string.h>

// ============================================================================
// Static allocation configuration
// ============================================================================

// Total pixel budget across all rasters
#ifndef PB_RASTER_POOL_SIZE
#define PB_RASTER_POOL_SIZE 8192
#endif

// ============================================================================
// Internal structures
// ============================================================================

/** Internal raster structure */
struct pb_raster {
    pb_raster_config_t config;
    pb_driver_t* driver;
    pb_color_t* pixels;              // width * height pixel buffer
    pb_pixel_address_t* mapping;     // Precomputed physical addresses
    size_t pool_offset;              // Offset into static pools
    size_t pixel_count;              // Size of this raster
    bool active;
};

// Forward declaration to access driver internals
extern const pb_driver_config_t* pb_driver_get_config(const pb_driver_t* driver);

// ============================================================================
// Static allocation pools
// ============================================================================

static pb_raster_t raster_storage[PB_MAX_RASTERS];
static pb_color_t pixel_pool[PB_RASTER_POOL_SIZE];
static pb_pixel_address_t mapping_pool[PB_RASTER_POOL_SIZE];
static size_t pool_used = 0;

// ============================================================================
// Mapping builder (matches cblinken logic)
// ============================================================================

static void build_mapping(pb_raster_t* raster) {
    const pb_raster_config_t* cfg = &raster->config;
    const pb_driver_config_t* drv_cfg = pb_driver_get_config(raster->driver);

    uint8_t num_strings = drv_cfg ? drv_cfg->num_strings : 32;
    uint8_t num_boards = drv_cfg ? drv_cfg->num_boards : 1;
    uint16_t num_pixels = drv_cfg ? drv_cfg->max_pixel_length : cfg->width;

    // CHAIN mode: simple linear mapping across chained strings
    if (cfg->wrap_mode == PB_WRAP_CHAIN) {
        uint16_t chain_len = cfg->chain_length ? cfg->chain_length : num_pixels;

        for (uint16_t y = 0; y < cfg->height; y++) {
            for (uint16_t x = 0; x < cfg->width; x++) {
                size_t idx = (size_t)y * cfg->width + x;
                size_t linear_pos = idx;

                uint16_t strings_offset = linear_pos / chain_len;
                uint16_t pixel_offset = linear_pos % chain_len;

                uint8_t string = cfg->start_string + strings_offset;
                uint8_t board = cfg->board;

                // Handle overflow to next board if needed
                while (string >= num_strings) {
                    string -= num_strings;
                    board++;
                    if (board >= num_boards) {
                        board = 0;
                    }
                }

                raster->mapping[idx].board = board;
                raster->mapping[idx].string = string;
                raster->mapping[idx].pixel = pixel_offset + cfg->start_pixel;
            }
        }
        return;
    }

    // Other modes: existing logic
    uint8_t board = cfg->board;
    uint8_t string = cfg->start_string;
    uint16_t pixel = 0;
    uint16_t offset = 0;
    uint16_t current_wrap = 0;

    for (uint16_t y = 0; y < cfg->height; y++) {
        for (uint16_t x = 0; x < cfg->width; x++) {
            size_t idx = (size_t)y * cfg->width + x;

            // Handle wrap mode at start of each row
            if (x == 0) {
                switch (cfg->wrap_mode) {
                    case PB_WRAP_NONE:
                        current_wrap = 0;
                        break;
                    case PB_WRAP_ZIGZAG:
                        current_wrap++;
                        break;
                    case PB_WRAP_CLIP:
                    default:
                        current_wrap = 0;
                        break;
                }
            }

            // Calculate offset based on wrap mode
            if (cfg->wrap_mode == PB_WRAP_ZIGZAG) {
                // Serpentine: odd wraps go backwards
                if (current_wrap % 2 == 0) {
                    offset = (cfg->width * current_wrap) - (pixel + 1 - (cfg->width * (current_wrap - 1)));
                } else {
                    offset = pixel;
                }
            } else if (cfg->wrap_mode == PB_WRAP_CLIP) {
                // CLIP: reset pixel at end of each row, advance string
                if (pixel >= cfg->width) {
                    pixel = 0;
                    current_wrap = 0;
                    string++;
                    if (string >= num_strings) {
                        string = 0;
                        board++;
                        if (board >= num_boards) {
                            board = 0;
                        }
                    }
                }
                offset = pixel;
            } else {
                // NO_WRAP: sequential
                offset = pixel;
            }

            // Store the mapping
            raster->mapping[idx].board = board;
            raster->mapping[idx].string = string;
            raster->mapping[idx].pixel = offset + cfg->start_pixel;

            // Advance pixel counter
            pixel++;
            if (pixel >= num_pixels) {
                pixel = 0;
                current_wrap = 0;
                string++;
                if (string >= num_strings) {
                    string = 0;
                    board++;
                    if (board >= num_boards) {
                        board = 0;
                    }
                }
            }
        }
    }
}

// ============================================================================
// Public API
// ============================================================================

int pb_raster_create(pb_driver_t* driver, const pb_raster_config_t* config) {
    if (driver == NULL || config == NULL) return -1;
    if (config->width == 0 || config->height == 0) return -1;

    const pb_driver_config_t* drv_cfg = pb_driver_get_config(driver);

    // Validate CHAIN mode configuration
    if (config->wrap_mode == PB_WRAP_CHAIN) {
        uint16_t chain_len = config->chain_length;
        if (chain_len == 0) {
            chain_len = drv_cfg ? drv_cfg->max_pixel_length : config->width;
        }

        // Width must be evenly divisible by chain_length
        if (config->width % chain_len != 0) {
            return -1;  // Width not divisible by chain length
        }

        // Check we have enough strings
        size_t total_pixels = (size_t)config->width * config->height;
        size_t strings_needed = (total_pixels + chain_len - 1) / chain_len;
        uint8_t num_strings = drv_cfg ? drv_cfg->num_strings : 32;
        if (config->start_string + strings_needed > num_strings) {
            return -1;  // Not enough strings available
        }
    }

    size_t pixel_count = (size_t)config->width * config->height;

    // Check if we have enough pool space
    if (pool_used + pixel_count > PB_RASTER_POOL_SIZE) return -1;

    // Find a free slot
    int slot = -1;
    for (int i = 0; i < PB_MAX_RASTERS; i++) {
        if (!raster_storage[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -1;  // No free slots

    // Use static storage
    pb_raster_t* raster = &raster_storage[slot];
    memset(raster, 0, sizeof(pb_raster_t));
    memcpy(&raster->config, config, sizeof(pb_raster_config_t));
    raster->driver = driver;

    // Carve out space from static pools
    raster->pool_offset = pool_used;
    raster->pixel_count = pixel_count;
    raster->pixels = &pixel_pool[pool_used];
    raster->mapping = &mapping_pool[pool_used];
    pool_used += pixel_count;

    memset(raster->pixels, 0, pixel_count * sizeof(pb_color_t));

    // Build mapping based on wrap mode
    build_mapping(raster);

    raster->active = true;

    return slot;
}

pb_raster_t* pb_raster_get(pb_driver_t* driver, int raster_id) {
    (void)driver;  // Could verify driver matches if we stored it
    if (raster_id < 0 || raster_id >= PB_MAX_RASTERS) return NULL;
    if (!raster_storage[raster_id].active) return NULL;
    return &raster_storage[raster_id];
}

void pb_raster_destroy(pb_driver_t* driver, int raster_id) {
    (void)driver;
    if (raster_id < 0 || raster_id >= PB_MAX_RASTERS) return;

    pb_raster_t* raster = &raster_storage[raster_id];
    if (!raster->active) return;

    // If this raster is at the end of the pool, reclaim the space
    if (raster->pool_offset + raster->pixel_count == pool_used) {
        pool_used = raster->pool_offset;
    }
    // Otherwise the pool space is "leaked" until all rasters are destroyed

    raster->active = false;

    // Check if all rasters are inactive - reset pool if so
    bool all_inactive = true;
    for (int i = 0; i < PB_MAX_RASTERS; i++) {
        if (raster_storage[i].active) {
            all_inactive = false;
            break;
        }
    }
    if (all_inactive) {
        pool_used = 0;
    }
}

void pb_raster_set_pixel(pb_raster_t* raster, uint16_t x, uint16_t y, pb_color_t color) {
    if (raster == NULL) return;
    if (x >= raster->config.width || y >= raster->config.height) return;

    size_t idx = (size_t)y * raster->config.width + x;
    raster->pixels[idx] = color;
}

pb_color_t pb_raster_get_pixel(const pb_raster_t* raster, uint16_t x, uint16_t y) {
    if (raster == NULL) return 0;
    if (x >= raster->config.width || y >= raster->config.height) return 0;

    size_t idx = (size_t)y * raster->config.width + x;
    return raster->pixels[idx];
}

void pb_raster_fill(pb_raster_t* raster, pb_color_t color) {
    if (raster == NULL) return;

    size_t pixel_count = (size_t)raster->config.width * raster->config.height;
    for (size_t i = 0; i < pixel_count; i++) {
        raster->pixels[i] = color;
    }
}

uint16_t pb_raster_get_width(const pb_raster_t* raster) {
    if (raster == NULL) return 0;
    return raster->config.width;
}

uint16_t pb_raster_get_height(const pb_raster_t* raster) {
    if (raster == NULL) return 0;
    return raster->config.height;
}

void pb_raster_show(pb_driver_t* driver, pb_raster_t* raster) {
    if (driver == NULL || raster == NULL) return;

    size_t pixel_count = (size_t)raster->config.width * raster->config.height;

    // Copy each raster pixel to the LED buffer using precomputed mapping
    for (size_t i = 0; i < pixel_count; i++) {
        pb_pixel_address_t* addr = &raster->mapping[i];
        pb_set_pixel(driver, addr->board, addr->string, addr->pixel, raster->pixels[i]);
    }
}
