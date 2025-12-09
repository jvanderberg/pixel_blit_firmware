/**
 * pb_led_driver.h - High-speed parallel WS2811/WS2812 LED driver
 *
 * Standalone library for driving up to 32 LED strings in parallel
 * using a single PIO state machine on RP2350B.
 */

#ifndef PB_LED_DRIVER_H
#define PB_LED_DRIVER_H

#ifdef PB_LED_DRIVER_TEST_BUILD
    // Host test build - use standard headers
    #include <stdint.h>
    #include <stdbool.h>
    #include <stddef.h>
#else
    #include <stdint.h>
    #include <stdbool.h>
    #include <stddef.h>
#endif

// ============================================================================
// Compile-time maximums
// ============================================================================

#ifndef PB_MAX_BOARDS
#define PB_MAX_BOARDS 4
#endif

#ifndef PB_MAX_PIXELS
#define PB_MAX_PIXELS 256
#endif

#ifndef PB_MAX_STRINGS
#define PB_MAX_STRINGS 32
#endif

#ifndef PB_MAX_RASTERS
#define PB_MAX_RASTERS 16
#endif

// ============================================================================
// Color type and utilities
// ============================================================================

/** Color represented as 0x00RRGGBB */
typedef uint32_t pb_color_t;

/** Create color from RGB components */
pb_color_t pb_color_rgb(uint8_t r, uint8_t g, uint8_t b);

/** Create color from HSV components (h: 0-255, s: 0-255, v: 0-255) */
pb_color_t pb_color_hsv(uint8_t h, uint8_t s, uint8_t v);

/** Scale color brightness (scale: 0-255, 255 = full brightness) */
pb_color_t pb_color_scale(pb_color_t color, uint8_t scale);

/** Blend two colors (amount: 0-255, 0 = all c1, 255 = all c2) */
pb_color_t pb_color_blend(pb_color_t c1, pb_color_t c2, uint8_t amount);

/** Extract red component */
static inline uint8_t pb_color_r(pb_color_t c) { return (c >> 16) & 0xFF; }

/** Extract green component */
static inline uint8_t pb_color_g(pb_color_t c) { return (c >> 8) & 0xFF; }

/** Extract blue component */
static inline uint8_t pb_color_b(pb_color_t c) { return c & 0xFF; }

// ============================================================================
// Configuration types
// ============================================================================

/** LED color order */
typedef enum {
    PB_COLOR_ORDER_GRB = 0,  // WS2812
    PB_COLOR_ORDER_RGB,      // WS2811
    PB_COLOR_ORDER_BGR,
    PB_COLOR_ORDER_RBG,
    PB_COLOR_ORDER_GBR,
    PB_COLOR_ORDER_BRG,
} pb_color_order_t;

/** Per-string configuration */
typedef struct {
    uint16_t length;    // Number of pixels (0 = unused)
    bool enabled;       // Whether this string is active
} pb_string_config_t;

/** Driver configuration */
typedef struct {
    uint8_t board_id;                              // This board's ID (0 = main)
    uint8_t num_boards;                            // Total boards in system
    uint8_t gpio_base;                             // First GPIO pin (typically 0)
    uint8_t num_strings;                           // Number of strings (1-32)
    pb_string_config_t strings[PB_MAX_STRINGS];    // Per-string config
    uint16_t max_pixel_length;                     // Max pixels in any string
    uint32_t frequency_hz;                         // Bit frequency (800000 typical)
    pb_color_order_t color_order;                  // Pixel color order
    uint16_t reset_us;                             // Reset time in microseconds
    uint8_t pio_index;                             // Which PIO (0 or 1)
} pb_driver_config_t;

// ============================================================================
// Bit-plane types (internal but exposed for testing)
// ============================================================================

#define PB_VALUE_PLANES 8

/** Bit-plane encoded pixel data for one color channel at one position */
typedef struct {
    uint32_t planes[PB_VALUE_PLANES];  // planes[0]=MSB, planes[7]=LSB
} pb_value_bits_t;

// ============================================================================
// Driver handle
// ============================================================================

typedef struct pb_driver pb_driver_t;

// ============================================================================
// Driver lifecycle
// ============================================================================

/** Initialize driver with configuration. Returns NULL on failure. */
pb_driver_t* pb_driver_init(const pb_driver_config_t* config);

/** Deinitialize driver and release resources */
void pb_driver_deinit(pb_driver_t* driver);

/** Get configuration (read-only) */
const pb_driver_config_t* pb_driver_get_config(const pb_driver_t* driver);

// ============================================================================
// Low-level API (direct string access)
// ============================================================================

/** Set a single pixel. Encodes to bit-plane immediately. */
void pb_set_pixel(pb_driver_t* driver, uint8_t board, uint8_t string,
                  uint16_t pixel, pb_color_t color);

/** Get pixel value from buffer */
pb_color_t pb_get_pixel(const pb_driver_t* driver, uint8_t board,
                        uint8_t string, uint16_t pixel);

/** Clear all pixels on a board to a color */
void pb_clear_board(pb_driver_t* driver, uint8_t board, pb_color_t color);

/** Clear all pixels on all boards to a color */
void pb_clear_all(pb_driver_t* driver, pb_color_t color);

/** Trigger DMA output (blocking) */
void pb_show(pb_driver_t* driver);

/** Trigger DMA output (non-blocking) */
void pb_show_async(pb_driver_t* driver);

/** Wait for async show to complete */
void pb_show_wait(pb_driver_t* driver);

/** Check if async show is in progress */
bool pb_show_busy(const pb_driver_t* driver);

/** Show with frame rate limiting */
void pb_show_with_fps(pb_driver_t* driver, uint16_t target_fps);

// ============================================================================
// Statistics
// ============================================================================

/** Get measured frames per second */
uint16_t pb_get_fps(const pb_driver_t* driver);

/** Get total frame count since init */
uint32_t pb_get_frame_count(const pb_driver_t* driver);

// ============================================================================
// Global brightness control
// ============================================================================

/** Set global brightness multiplier (0-255, applied to all pixels) */
void pb_set_global_brightness(uint8_t brightness);

/** Get current global brightness */
uint8_t pb_get_global_brightness(void);

// ============================================================================
// Raster abstraction layer
// ============================================================================

/** Physical pixel address - maps virtual (x,y) to hardware location */
typedef struct {
    uint8_t board;
    uint8_t string;
    uint16_t pixel;
} pb_pixel_address_t;

/** Raster wrap mode for mapping 2D to physical LEDs */
typedef enum {
    PB_WRAP_CLIP = 0,       // Each row is one string, reset pixel to 0 at row end
    PB_WRAP_NONE,           // Sequential pixels, no special handling
    PB_WRAP_ZIGZAG,         // Serpentine - alternating direction for folded strips
    PB_WRAP_CHAIN,          // Chain multiple strings into longer virtual rows
} pb_wrap_mode_t;

/** Raster configuration */
typedef struct {
    uint16_t width;             // Raster width in pixels
    uint16_t height;            // Raster height in pixels
    uint8_t board;              // Starting board
    uint8_t start_string;       // First string in mapping
    uint16_t start_pixel;       // First pixel offset
    pb_wrap_mode_t wrap_mode;
    uint16_t chain_length;      // For CHAIN mode: pixels per physical string (0 = use max_pixel_length)
} pb_raster_config_t;

/** Raster handle */
typedef struct pb_raster pb_raster_t;

/** Create a raster. Returns raster ID or -1 on failure. */
int pb_raster_create(pb_driver_t* driver, const pb_raster_config_t* config);

/** Get raster by ID. Returns NULL if invalid. */
pb_raster_t* pb_raster_get(pb_driver_t* driver, int raster_id);

/** Destroy a raster by ID */
void pb_raster_destroy(pb_driver_t* driver, int raster_id);

/** Destroy all rasters associated with a driver (called on driver deinit) */
void pb_raster_destroy_all(pb_driver_t* driver);

/** Set pixel in raster (x, y coordinates) */
void pb_raster_set_pixel(pb_raster_t* raster, uint16_t x, uint16_t y, pb_color_t color);

/** Get pixel from raster */
pb_color_t pb_raster_get_pixel(const pb_raster_t* raster, uint16_t x, uint16_t y);

/** Fill entire raster with color */
void pb_raster_fill(pb_raster_t* raster, pb_color_t color);

/** Get raster dimensions */
uint16_t pb_raster_get_width(const pb_raster_t* raster);
uint16_t pb_raster_get_height(const pb_raster_t* raster);

/** Copy raster to LED buffer using precomputed mapping */
void pb_raster_show(pb_driver_t* driver, pb_raster_t* raster);

#endif // PB_LED_DRIVER_H
