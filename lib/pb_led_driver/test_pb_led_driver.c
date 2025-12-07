/**
 * test_pb_led_driver.c - TDD tests for pb_led_driver
 *
 * Build: mkdir build && cd build && cmake -DPB_LED_DRIVER_TEST=ON .. && make
 * Run: ./pb_led_driver_test
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "pb_led_driver.h"

// ============================================================================
// Test utilities
// ============================================================================

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    static void test_##name(void); \
    static void run_test_##name(void) { \
        printf("  TEST: %s ... ", #name); \
        tests_run++; \
        test_##name(); \
        tests_passed++; \
        printf("PASS\n"); \
    } \
    static void test_##name(void)

#define RUN_TEST(name) run_test_##name()

#define ASSERT_EQ(expected, actual) \
    do { \
        if ((expected) != (actual)) { \
            printf("FAIL\n    Expected: 0x%X, Got: 0x%X at line %d\n", \
                   (unsigned int)(expected), (unsigned int)(actual), __LINE__); \
            exit(1); \
        } \
    } while(0)

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            printf("FAIL\n    Condition false at line %d\n", __LINE__); \
            exit(1); \
        } \
    } while(0)

// ============================================================================
// Color utility tests
// ============================================================================

TEST(color_rgb_creates_correct_value) {
    pb_color_t c = pb_color_rgb(0xFF, 0x80, 0x40);
    ASSERT_EQ(0x00FF8040, c);
}

TEST(color_rgb_zero) {
    pb_color_t c = pb_color_rgb(0, 0, 0);
    ASSERT_EQ(0x00000000, c);
}

TEST(color_rgb_white) {
    pb_color_t c = pb_color_rgb(255, 255, 255);
    ASSERT_EQ(0x00FFFFFF, c);
}

TEST(color_component_extraction) {
    pb_color_t c = 0x00AABBCC;
    ASSERT_EQ(0xAA, pb_color_r(c));
    ASSERT_EQ(0xBB, pb_color_g(c));
    ASSERT_EQ(0xCC, pb_color_b(c));
}

TEST(color_scale_full) {
    pb_color_t c = pb_color_rgb(100, 100, 100);
    pb_color_t scaled = pb_color_scale(c, 255);
    // At 255, should be nearly unchanged (may lose 1 due to rounding)
    ASSERT_TRUE(pb_color_r(scaled) >= 99);
    ASSERT_TRUE(pb_color_g(scaled) >= 99);
    ASSERT_TRUE(pb_color_b(scaled) >= 99);
}

TEST(color_scale_half) {
    pb_color_t c = pb_color_rgb(100, 200, 50);
    pb_color_t scaled = pb_color_scale(c, 128);
    // At 128/255 (~50%), values should be roughly halved
    ASSERT_TRUE(pb_color_r(scaled) >= 49 && pb_color_r(scaled) <= 51);
    ASSERT_TRUE(pb_color_g(scaled) >= 99 && pb_color_g(scaled) <= 101);
    ASSERT_TRUE(pb_color_b(scaled) >= 24 && pb_color_b(scaled) <= 26);
}

TEST(color_scale_zero) {
    pb_color_t c = pb_color_rgb(255, 255, 255);
    pb_color_t scaled = pb_color_scale(c, 0);
    ASSERT_EQ(0x00000000, scaled);
}

TEST(color_blend_all_c1) {
    pb_color_t c1 = pb_color_rgb(255, 0, 0);
    pb_color_t c2 = pb_color_rgb(0, 255, 0);
    pb_color_t blended = pb_color_blend(c1, c2, 0);
    ASSERT_EQ(0x00FF0000, blended);
}

TEST(color_blend_all_c2) {
    pb_color_t c1 = pb_color_rgb(255, 0, 0);
    pb_color_t c2 = pb_color_rgb(0, 255, 0);
    pb_color_t blended = pb_color_blend(c1, c2, 255);
    ASSERT_EQ(0x0000FF00, blended);
}

TEST(color_blend_halfway) {
    pb_color_t c1 = pb_color_rgb(100, 0, 200);
    pb_color_t c2 = pb_color_rgb(0, 100, 0);
    pb_color_t blended = pb_color_blend(c1, c2, 128);
    // At 128/255, should be roughly halfway
    ASSERT_TRUE(pb_color_r(blended) >= 49 && pb_color_r(blended) <= 51);
    ASSERT_TRUE(pb_color_g(blended) >= 49 && pb_color_g(blended) <= 51);
    ASSERT_TRUE(pb_color_b(blended) >= 99 && pb_color_b(blended) <= 101);
}

TEST(color_hsv_red) {
    // H=0 should be red
    pb_color_t c = pb_color_hsv(0, 255, 255);
    ASSERT_EQ(255, pb_color_r(c));
    ASSERT_EQ(0, pb_color_g(c));
    ASSERT_EQ(0, pb_color_b(c));
}

TEST(color_hsv_green) {
    // H=85 (1/3 of 255) should be green
    pb_color_t c = pb_color_hsv(85, 255, 255);
    ASSERT_TRUE(pb_color_r(c) <= 5);  // Allow small rounding error
    ASSERT_EQ(255, pb_color_g(c));
    ASSERT_TRUE(pb_color_b(c) <= 5);  // Allow small rounding error
}

TEST(color_hsv_blue) {
    // H=170 (2/3 of 255) should be blue
    pb_color_t c = pb_color_hsv(170, 255, 255);
    ASSERT_TRUE(pb_color_r(c) <= 5);  // Allow small rounding error
    ASSERT_TRUE(pb_color_g(c) <= 5);  // Allow small rounding error
    ASSERT_EQ(255, pb_color_b(c));
}

TEST(color_hsv_white) {
    // S=0 should give white regardless of H
    pb_color_t c = pb_color_hsv(100, 0, 255);
    ASSERT_EQ(255, pb_color_r(c));
    ASSERT_EQ(255, pb_color_g(c));
    ASSERT_EQ(255, pb_color_b(c));
}

TEST(color_hsv_black) {
    // V=0 should give black regardless of H and S
    pb_color_t c = pb_color_hsv(100, 255, 0);
    ASSERT_EQ(0x00000000, c);
}

// ============================================================================
// Driver init tests
// ============================================================================

TEST(driver_init_returns_valid_pointer) {
    pb_driver_config_t config = {
        .board_id = 0,
        .num_boards = 1,
        .gpio_base = 0,
        .num_strings = 4,
        .max_pixel_length = 10,
        .frequency_hz = 800000,
        .color_order = PB_COLOR_ORDER_GRB,
        .reset_us = 200,
        .pio_index = 0,
    };
    for (int i = 0; i < 4; i++) {
        config.strings[i].length = 10;
        config.strings[i].enabled = true;
    }

    pb_driver_t* driver = pb_driver_init(&config);
    ASSERT_TRUE(driver != NULL);
    pb_driver_deinit(driver);
}

TEST(driver_get_config_returns_correct_values) {
    pb_driver_config_t config = {
        .board_id = 0,
        .num_boards = 2,
        .gpio_base = 0,
        .num_strings = 8,
        .max_pixel_length = 50,
        .frequency_hz = 800000,
        .color_order = PB_COLOR_ORDER_RGB,
        .reset_us = 200,
        .pio_index = 0,
    };

    pb_driver_t* driver = pb_driver_init(&config);
    ASSERT_TRUE(driver != NULL);

    const pb_driver_config_t* stored = pb_driver_get_config(driver);
    ASSERT_TRUE(stored != NULL);
    ASSERT_EQ(0, stored->board_id);
    ASSERT_EQ(2, stored->num_boards);
    ASSERT_EQ(8, stored->num_strings);
    ASSERT_EQ(50, stored->max_pixel_length);
    ASSERT_EQ(PB_COLOR_ORDER_RGB, stored->color_order);

    pb_driver_deinit(driver);
}

// ============================================================================
// Bit-plane encoding tests
// ============================================================================

TEST(set_pixel_basic) {
    pb_driver_config_t config = {
        .board_id = 0,
        .num_boards = 1,
        .num_strings = 4,
        .max_pixel_length = 10,
        .color_order = PB_COLOR_ORDER_GRB,
    };
    config.strings[0].length = 10;
    config.strings[0].enabled = true;

    pb_driver_t* driver = pb_driver_init(&config);
    ASSERT_TRUE(driver != NULL);

    // Set pixel and verify we can get it back
    pb_set_pixel(driver, 0, 0, 0, 0xFF0000);  // Red
    pb_color_t color = pb_get_pixel(driver, 0, 0, 0);
    ASSERT_EQ(0xFF0000, color);

    pb_driver_deinit(driver);
}

TEST(set_pixel_different_strings) {
    pb_driver_config_t config = {
        .board_id = 0,
        .num_boards = 1,
        .num_strings = 4,
        .max_pixel_length = 10,
        .color_order = PB_COLOR_ORDER_GRB,
    };
    for (int i = 0; i < 4; i++) {
        config.strings[i].length = 10;
        config.strings[i].enabled = true;
    }

    pb_driver_t* driver = pb_driver_init(&config);
    ASSERT_TRUE(driver != NULL);

    // Set different colors on different strings
    pb_set_pixel(driver, 0, 0, 0, 0xFF0000);  // String 0: Red
    pb_set_pixel(driver, 0, 1, 0, 0x00FF00);  // String 1: Green
    pb_set_pixel(driver, 0, 2, 0, 0x0000FF);  // String 2: Blue
    pb_set_pixel(driver, 0, 3, 0, 0xFFFFFF);  // String 3: White

    ASSERT_EQ(0xFF0000, pb_get_pixel(driver, 0, 0, 0));
    ASSERT_EQ(0x00FF00, pb_get_pixel(driver, 0, 1, 0));
    ASSERT_EQ(0x0000FF, pb_get_pixel(driver, 0, 2, 0));
    ASSERT_EQ(0xFFFFFF, pb_get_pixel(driver, 0, 3, 0));

    pb_driver_deinit(driver);
}

TEST(set_pixel_different_positions) {
    pb_driver_config_t config = {
        .board_id = 0,
        .num_boards = 1,
        .num_strings = 1,
        .max_pixel_length = 10,
        .color_order = PB_COLOR_ORDER_GRB,
    };
    config.strings[0].length = 10;
    config.strings[0].enabled = true;

    pb_driver_t* driver = pb_driver_init(&config);
    ASSERT_TRUE(driver != NULL);

    // Set different colors at different pixel positions
    pb_set_pixel(driver, 0, 0, 0, 0x112233);
    pb_set_pixel(driver, 0, 0, 5, 0x445566);
    pb_set_pixel(driver, 0, 0, 9, 0x778899);

    ASSERT_EQ(0x112233, pb_get_pixel(driver, 0, 0, 0));
    ASSERT_EQ(0x445566, pb_get_pixel(driver, 0, 0, 5));
    ASSERT_EQ(0x778899, pb_get_pixel(driver, 0, 0, 9));

    pb_driver_deinit(driver);
}

TEST(clear_board_sets_all_pixels) {
    pb_driver_config_t config = {
        .board_id = 0,
        .num_boards = 1,
        .num_strings = 2,
        .max_pixel_length = 5,
        .color_order = PB_COLOR_ORDER_GRB,
    };
    config.strings[0].length = 5;
    config.strings[0].enabled = true;
    config.strings[1].length = 5;
    config.strings[1].enabled = true;

    pb_driver_t* driver = pb_driver_init(&config);
    ASSERT_TRUE(driver != NULL);

    // Set some pixels first
    pb_set_pixel(driver, 0, 0, 0, 0xFF0000);
    pb_set_pixel(driver, 0, 1, 2, 0x00FF00);

    // Clear to blue
    pb_clear_board(driver, 0, 0x0000FF);

    // All pixels should now be blue
    ASSERT_EQ(0x0000FF, pb_get_pixel(driver, 0, 0, 0));
    ASSERT_EQ(0x0000FF, pb_get_pixel(driver, 0, 0, 4));
    ASSERT_EQ(0x0000FF, pb_get_pixel(driver, 0, 1, 0));
    ASSERT_EQ(0x0000FF, pb_get_pixel(driver, 0, 1, 4));

    pb_driver_deinit(driver);
}

// ============================================================================
// Raster creation tests
// ============================================================================

TEST(raster_create_returns_valid_id) {
    pb_driver_config_t config = {
        .board_id = 0,
        .num_boards = 1,
        .num_strings = 4,
        .max_pixel_length = 10,
        .color_order = PB_COLOR_ORDER_GRB,
    };
    for (int i = 0; i < 4; i++) {
        config.strings[i].length = 10;
        config.strings[i].enabled = true;
    }

    pb_driver_t* driver = pb_driver_init(&config);
    ASSERT_TRUE(driver != NULL);

    pb_raster_config_t raster_config = {
        .width = 5,
        .height = 3,
        .board = 0,
        .start_string = 0,
        .start_pixel = 0,
        .wrap_mode = PB_WRAP_CLIP,
    };

    int raster_id = pb_raster_create(driver, &raster_config);
    ASSERT_TRUE(raster_id >= 0);

    pb_raster_t* raster = pb_raster_get(driver, raster_id);
    ASSERT_TRUE(raster != NULL);

    pb_driver_deinit(driver);
}

TEST(raster_get_dimensions) {
    pb_driver_config_t config = {
        .board_id = 0,
        .num_boards = 1,
        .num_strings = 4,
        .max_pixel_length = 20,
        .color_order = PB_COLOR_ORDER_GRB,
    };
    for (int i = 0; i < 4; i++) {
        config.strings[i].length = 20;
        config.strings[i].enabled = true;
    }

    pb_driver_t* driver = pb_driver_init(&config);
    ASSERT_TRUE(driver != NULL);

    pb_raster_config_t raster_config = {
        .width = 8,
        .height = 4,
        .board = 0,
        .start_string = 0,
        .start_pixel = 0,
        .wrap_mode = PB_WRAP_CLIP,
    };

    int raster_id = pb_raster_create(driver, &raster_config);
    pb_raster_t* raster = pb_raster_get(driver, raster_id);
    ASSERT_TRUE(raster != NULL);

    ASSERT_EQ(8, pb_raster_get_width(raster));
    ASSERT_EQ(4, pb_raster_get_height(raster));

    pb_driver_deinit(driver);
}

TEST(raster_set_get_pixel) {
    pb_driver_config_t config = {
        .board_id = 0,
        .num_boards = 1,
        .num_strings = 4,
        .max_pixel_length = 10,
        .color_order = PB_COLOR_ORDER_GRB,
    };
    for (int i = 0; i < 4; i++) {
        config.strings[i].length = 10;
        config.strings[i].enabled = true;
    }

    pb_driver_t* driver = pb_driver_init(&config);
    pb_raster_config_t raster_config = {
        .width = 5,
        .height = 3,
        .board = 0,
        .start_string = 0,
        .start_pixel = 0,
        .wrap_mode = PB_WRAP_CLIP,
    };

    int raster_id = pb_raster_create(driver, &raster_config);
    pb_raster_t* raster = pb_raster_get(driver, raster_id);

    // Set some pixels
    pb_raster_set_pixel(raster, 0, 0, 0xFF0000);
    pb_raster_set_pixel(raster, 4, 0, 0x00FF00);
    pb_raster_set_pixel(raster, 2, 1, 0x0000FF);

    // Get them back
    ASSERT_EQ(0xFF0000, pb_raster_get_pixel(raster, 0, 0));
    ASSERT_EQ(0x00FF00, pb_raster_get_pixel(raster, 4, 0));
    ASSERT_EQ(0x0000FF, pb_raster_get_pixel(raster, 2, 1));

    pb_driver_deinit(driver);
}

TEST(raster_fill) {
    pb_driver_config_t config = {
        .board_id = 0,
        .num_boards = 1,
        .num_strings = 4,
        .max_pixel_length = 10,
        .color_order = PB_COLOR_ORDER_GRB,
    };
    for (int i = 0; i < 4; i++) {
        config.strings[i].length = 10;
        config.strings[i].enabled = true;
    }

    pb_driver_t* driver = pb_driver_init(&config);
    pb_raster_config_t raster_config = {
        .width = 3,
        .height = 2,
        .board = 0,
        .start_string = 0,
        .start_pixel = 0,
        .wrap_mode = PB_WRAP_CLIP,
    };

    int raster_id = pb_raster_create(driver, &raster_config);
    pb_raster_t* raster = pb_raster_get(driver, raster_id);

    // Fill with cyan
    pb_raster_fill(raster, 0x00FFFF);

    // All pixels should be cyan
    ASSERT_EQ(0x00FFFF, pb_raster_get_pixel(raster, 0, 0));
    ASSERT_EQ(0x00FFFF, pb_raster_get_pixel(raster, 2, 0));
    ASSERT_EQ(0x00FFFF, pb_raster_get_pixel(raster, 0, 1));
    ASSERT_EQ(0x00FFFF, pb_raster_get_pixel(raster, 2, 1));

    pb_driver_deinit(driver);
}

// ============================================================================
// Raster mapping tests
// ============================================================================

TEST(raster_show_continuous_mapping) {
    // Create a 5x2 raster mapped to strings 0 and 1
    // Continuous: row 0 = string 0 pixels 0-4, row 1 = string 1 pixels 0-4
    pb_driver_config_t config = {
        .board_id = 0,
        .num_boards = 1,
        .num_strings = 4,
        .max_pixel_length = 10,
        .color_order = PB_COLOR_ORDER_GRB,
    };
    for (int i = 0; i < 4; i++) {
        config.strings[i].length = 10;
        config.strings[i].enabled = true;
    }

    pb_driver_t* driver = pb_driver_init(&config);
    pb_raster_config_t raster_config = {
        .width = 5,
        .height = 2,
        .board = 0,
        .start_string = 0,
        .start_pixel = 0,
        .wrap_mode = PB_WRAP_CLIP,
    };

    int raster_id = pb_raster_create(driver, &raster_config);
    pb_raster_t* raster = pb_raster_get(driver, raster_id);

    // Set pixels in raster
    pb_raster_set_pixel(raster, 0, 0, 0xFF0000);  // (0,0) -> string 0, pixel 0
    pb_raster_set_pixel(raster, 4, 0, 0x00FF00);  // (4,0) -> string 0, pixel 4
    pb_raster_set_pixel(raster, 0, 1, 0x0000FF);  // (0,1) -> string 1, pixel 0
    pb_raster_set_pixel(raster, 2, 1, 0xFFFF00);  // (2,1) -> string 1, pixel 2

    // Show raster (copy to LED buffer)
    pb_raster_show(driver, raster);

    // Verify LED buffer has correct values
    ASSERT_EQ(0xFF0000, pb_get_pixel(driver, 0, 0, 0));  // string 0, pixel 0
    ASSERT_EQ(0x00FF00, pb_get_pixel(driver, 0, 0, 4));  // string 0, pixel 4
    ASSERT_EQ(0x0000FF, pb_get_pixel(driver, 0, 1, 0));  // string 1, pixel 0
    ASSERT_EQ(0xFFFF00, pb_get_pixel(driver, 0, 1, 2));  // string 1, pixel 2

    pb_driver_deinit(driver);
}

TEST(raster_show_zigzag_mapping) {
    // Create a 5x2 raster with zigzag (serpentine) mapping
    // Zigzag (cblinken WRAP): single strip folded back and forth
    //   row 0 = string 0, pixels 0-4 (left-to-right)
    //   row 1 = string 0, pixels 9-5 (right-to-left, continuing the strip)
    pb_driver_config_t config = {
        .board_id = 0,
        .num_boards = 1,
        .num_strings = 4,
        .max_pixel_length = 20,  // Need enough pixels for serpentine
        .color_order = PB_COLOR_ORDER_GRB,
    };
    for (int i = 0; i < 4; i++) {
        config.strings[i].length = 20;
        config.strings[i].enabled = true;
    }

    pb_driver_t* driver = pb_driver_init(&config);
    pb_raster_config_t raster_config = {
        .width = 5,
        .height = 2,
        .board = 0,
        .start_string = 0,
        .start_pixel = 0,
        .wrap_mode = PB_WRAP_ZIGZAG,
    };

    int raster_id = pb_raster_create(driver, &raster_config);
    pb_raster_t* raster = pb_raster_get(driver, raster_id);

    // Set pixels in raster
    pb_raster_set_pixel(raster, 0, 0, 0xFF0000);  // (0,0) -> string 0, pixel 0
    pb_raster_set_pixel(raster, 4, 0, 0x00FF00);  // (4,0) -> string 0, pixel 4
    pb_raster_set_pixel(raster, 0, 1, 0x0000FF);  // (0,1) -> string 0, pixel 9 (serpentine!)
    pb_raster_set_pixel(raster, 4, 1, 0xFFFF00);  // (4,1) -> string 0, pixel 5 (serpentine!)

    // Show raster (copy to LED buffer)
    pb_raster_show(driver, raster);

    // Verify LED buffer - row 0 is normal (pixels 0-4)
    ASSERT_EQ(0xFF0000, pb_get_pixel(driver, 0, 0, 0));  // string 0, pixel 0
    ASSERT_EQ(0x00FF00, pb_get_pixel(driver, 0, 0, 4));  // string 0, pixel 4

    // Row 1 is reversed on same string (pixels 9-5, serpentine)
    ASSERT_EQ(0x0000FF, pb_get_pixel(driver, 0, 0, 9));  // string 0, pixel 9 (was x=0)
    ASSERT_EQ(0xFFFF00, pb_get_pixel(driver, 0, 0, 5));  // string 0, pixel 5 (was x=4)

    pb_driver_deinit(driver);
}

TEST(raster_multiple_rasters) {
    pb_driver_config_t config = {
        .board_id = 0,
        .num_boards = 1,
        .num_strings = 8,
        .max_pixel_length = 10,
        .color_order = PB_COLOR_ORDER_GRB,
    };
    for (int i = 0; i < 8; i++) {
        config.strings[i].length = 10;
        config.strings[i].enabled = true;
    }

    pb_driver_t* driver = pb_driver_init(&config);

    // Create first raster on strings 0-1
    pb_raster_config_t raster1_config = {
        .width = 5,
        .height = 2,
        .board = 0,
        .start_string = 0,
        .start_pixel = 0,
        .wrap_mode = PB_WRAP_CLIP,
    };
    int id1 = pb_raster_create(driver, &raster1_config);
    ASSERT_TRUE(id1 >= 0);

    // Create second raster on strings 2-3
    pb_raster_config_t raster2_config = {
        .width = 5,
        .height = 2,
        .board = 0,
        .start_string = 2,
        .start_pixel = 0,
        .wrap_mode = PB_WRAP_CLIP,
    };
    int id2 = pb_raster_create(driver, &raster2_config);
    ASSERT_TRUE(id2 >= 0);
    ASSERT_TRUE(id1 != id2);

    // Get both rasters
    pb_raster_t* raster1 = pb_raster_get(driver, id1);
    pb_raster_t* raster2 = pb_raster_get(driver, id2);
    ASSERT_TRUE(raster1 != NULL);
    ASSERT_TRUE(raster2 != NULL);
    ASSERT_TRUE(raster1 != raster2);

    pb_driver_deinit(driver);
}

TEST(raster_destroy_frees_slot) {
    pb_driver_config_t config = {
        .board_id = 0,
        .num_boards = 1,
        .num_strings = 4,
        .max_pixel_length = 10,
        .color_order = PB_COLOR_ORDER_GRB,
    };
    for (int i = 0; i < 4; i++) {
        config.strings[i].length = 10;
        config.strings[i].enabled = true;
    }

    pb_driver_t* driver = pb_driver_init(&config);
    pb_raster_config_t raster_config = {
        .width = 3,
        .height = 2,
        .board = 0,
        .start_string = 0,
        .start_pixel = 0,
        .wrap_mode = PB_WRAP_CLIP,
    };

    // Create and destroy raster
    int id1 = pb_raster_create(driver, &raster_config);
    ASSERT_TRUE(id1 >= 0);
    ASSERT_TRUE(pb_raster_get(driver, id1) != NULL);

    pb_raster_destroy(driver, id1);
    ASSERT_TRUE(pb_raster_get(driver, id1) == NULL);

    // Should be able to create a new raster in the freed slot
    int id2 = pb_raster_create(driver, &raster_config);
    ASSERT_TRUE(id2 >= 0);
    ASSERT_TRUE(pb_raster_get(driver, id2) != NULL);

    pb_driver_deinit(driver);
}

TEST(raster_zigzag_multi_fold_multi_string) {
    // Test serpentine folding: 4 rows per string, then advance to next string
    // 2 strings × 20 pixels each, folded into 8 rows × 5 pixels
    pb_driver_config_t config = {
        .board_id = 0,
        .num_boards = 1,
        .num_strings = 4,
        .max_pixel_length = 20,
        .color_order = PB_COLOR_ORDER_GRB,
    };
    for (int i = 0; i < 4; i++) {
        config.strings[i].length = 20;
        config.strings[i].enabled = true;
    }

    pb_driver_t* driver = pb_driver_init(&config);
    pb_raster_config_t raster_config = {
        .width = 5,           // 20 pixels ÷ 4 folds = 5 per row
        .height = 8,          // 2 strings × 4 rows per string
        .board = 0,
        .start_string = 0,
        .start_pixel = 0,
        .wrap_mode = PB_WRAP_ZIGZAG,
    };

    int raster_id = pb_raster_create(driver, &raster_config);
    pb_raster_t* raster = pb_raster_get(driver, raster_id);

    // Set test pixels across the mapping
    // String 0, row 0: pixels 0-4 (left to right)
    pb_raster_set_pixel(raster, 0, 0, 0x000001);  // string 0, pixel 0
    pb_raster_set_pixel(raster, 4, 0, 0x000002);  // string 0, pixel 4

    // String 0, row 1: pixels 9-5 (right to left, serpentine)
    pb_raster_set_pixel(raster, 0, 1, 0x000003);  // string 0, pixel 9
    pb_raster_set_pixel(raster, 4, 1, 0x000004);  // string 0, pixel 5

    // String 0, row 2: pixels 10-14 (left to right)
    pb_raster_set_pixel(raster, 0, 2, 0x000005);  // string 0, pixel 10
    pb_raster_set_pixel(raster, 4, 2, 0x000006);  // string 0, pixel 14

    // String 0, row 3: pixels 19-15 (right to left, serpentine)
    pb_raster_set_pixel(raster, 0, 3, 0x000007);  // string 0, pixel 19
    pb_raster_set_pixel(raster, 4, 3, 0x000008);  // string 0, pixel 15

    // String 1, row 4: pixels 0-4 (left to right) - NEW STRING!
    pb_raster_set_pixel(raster, 0, 4, 0x000009);  // string 1, pixel 0
    pb_raster_set_pixel(raster, 4, 4, 0x00000A);  // string 1, pixel 4

    // String 1, row 5: pixels 9-5 (right to left, serpentine)
    pb_raster_set_pixel(raster, 0, 5, 0x00000B);  // string 1, pixel 9

    pb_raster_show(driver, raster);

    // Verify string 0 mapping (4 rows, serpentine)
    ASSERT_EQ(0x000001, pb_get_pixel(driver, 0, 0, 0));   // row 0: pixel 0
    ASSERT_EQ(0x000002, pb_get_pixel(driver, 0, 0, 4));   // row 0: pixel 4
    ASSERT_EQ(0x000003, pb_get_pixel(driver, 0, 0, 9));   // row 1: pixel 9 (serpentine)
    ASSERT_EQ(0x000004, pb_get_pixel(driver, 0, 0, 5));   // row 1: pixel 5 (serpentine)
    ASSERT_EQ(0x000005, pb_get_pixel(driver, 0, 0, 10));  // row 2: pixel 10
    ASSERT_EQ(0x000006, pb_get_pixel(driver, 0, 0, 14));  // row 2: pixel 14
    ASSERT_EQ(0x000007, pb_get_pixel(driver, 0, 0, 19));  // row 3: pixel 19 (serpentine)
    ASSERT_EQ(0x000008, pb_get_pixel(driver, 0, 0, 15));  // row 3: pixel 15 (serpentine)

    // Verify string 1 mapping (next 4 rows start fresh)
    ASSERT_EQ(0x000009, pb_get_pixel(driver, 0, 1, 0));   // row 4: string 1, pixel 0
    ASSERT_EQ(0x00000A, pb_get_pixel(driver, 0, 1, 4));   // row 4: string 1, pixel 4
    ASSERT_EQ(0x00000B, pb_get_pixel(driver, 0, 1, 9));   // row 5: string 1, pixel 9

    pb_driver_deinit(driver);
}

TEST(raster_with_start_offset) {
    // Test raster starting at string 2, pixel 3
    pb_driver_config_t config = {
        .board_id = 0,
        .num_boards = 1,
        .num_strings = 8,
        .max_pixel_length = 20,
        .color_order = PB_COLOR_ORDER_GRB,
    };
    for (int i = 0; i < 8; i++) {
        config.strings[i].length = 20;
        config.strings[i].enabled = true;
    }

    pb_driver_t* driver = pb_driver_init(&config);
    pb_raster_config_t raster_config = {
        .width = 4,
        .height = 2,
        .board = 0,
        .start_string = 2,
        .start_pixel = 3,
        .wrap_mode = PB_WRAP_CLIP,
    };

    int id = pb_raster_create(driver, &raster_config);
    pb_raster_t* raster = pb_raster_get(driver, id);

    // Set a pixel at raster (1, 0)
    pb_raster_set_pixel(raster, 1, 0, 0xAABBCC);
    // Set a pixel at raster (2, 1)
    pb_raster_set_pixel(raster, 2, 1, 0x112233);

    pb_raster_show(driver, raster);

    // (1,0) should map to string 2, pixel 4 (start_pixel 3 + x 1)
    ASSERT_EQ(0xAABBCC, pb_get_pixel(driver, 0, 2, 4));
    // (2,1) should map to string 3, pixel 5 (start_pixel 3 + x 2)
    ASSERT_EQ(0x112233, pb_get_pixel(driver, 0, 3, 5));

    pb_driver_deinit(driver);
}

TEST(raster_chain_mode_basic) {
    // Test CHAIN mode: 4 strings × 25 pixels chained into 50×2 raster
    pb_driver_config_t config = {
        .board_id = 0,
        .num_boards = 1,
        .num_strings = 8,
        .max_pixel_length = 50,
        .color_order = PB_COLOR_ORDER_GRB,
    };
    for (int i = 0; i < 8; i++) {
        config.strings[i].length = 50;
        config.strings[i].enabled = true;
    }

    pb_driver_t* driver = pb_driver_init(&config);

    // 50 wide × 2 tall = 100 pixels
    // 4 strings × 25 pixels each = 100 pixels
    // Row 0: string 0 (0-24), string 1 (25-49)
    // Row 1: string 2 (0-24), string 3 (25-49)
    pb_raster_config_t raster_config = {
        .width = 50,
        .height = 2,
        .board = 0,
        .start_string = 0,
        .start_pixel = 0,
        .wrap_mode = PB_WRAP_CHAIN,
        .chain_length = 25,
    };

    int id = pb_raster_create(driver, &raster_config);
    ASSERT_TRUE(id >= 0);

    pb_raster_t* raster = pb_raster_get(driver, id);
    ASSERT_TRUE(raster != NULL);

    // Set pixels at boundaries
    pb_raster_set_pixel(raster, 0, 0, 0x000001);   // string 0, pixel 0
    pb_raster_set_pixel(raster, 24, 0, 0x000002);  // string 0, pixel 24
    pb_raster_set_pixel(raster, 25, 0, 0x000003);  // string 1, pixel 0
    pb_raster_set_pixel(raster, 49, 0, 0x000004);  // string 1, pixel 24
    pb_raster_set_pixel(raster, 0, 1, 0x000005);   // string 2, pixel 0
    pb_raster_set_pixel(raster, 25, 1, 0x000006);  // string 3, pixel 0
    pb_raster_set_pixel(raster, 49, 1, 0x000007);  // string 3, pixel 24

    pb_raster_show(driver, raster);

    // Verify mapping
    ASSERT_EQ(0x000001, pb_get_pixel(driver, 0, 0, 0));   // string 0, pixel 0
    ASSERT_EQ(0x000002, pb_get_pixel(driver, 0, 0, 24));  // string 0, pixel 24
    ASSERT_EQ(0x000003, pb_get_pixel(driver, 0, 1, 0));   // string 1, pixel 0
    ASSERT_EQ(0x000004, pb_get_pixel(driver, 0, 1, 24));  // string 1, pixel 24
    ASSERT_EQ(0x000005, pb_get_pixel(driver, 0, 2, 0));   // string 2, pixel 0
    ASSERT_EQ(0x000006, pb_get_pixel(driver, 0, 3, 0));   // string 3, pixel 0
    ASSERT_EQ(0x000007, pb_get_pixel(driver, 0, 3, 24));  // string 3, pixel 24

    pb_driver_deinit(driver);
}

TEST(raster_chain_mode_validation) {
    // Test CHAIN mode validation: width not divisible by chain_length should fail
    pb_driver_config_t config = {
        .board_id = 0,
        .num_boards = 1,
        .num_strings = 4,
        .max_pixel_length = 50,
        .color_order = PB_COLOR_ORDER_GRB,
    };
    for (int i = 0; i < 4; i++) {
        config.strings[i].length = 50;
        config.strings[i].enabled = true;
    }

    pb_driver_t* driver = pb_driver_init(&config);

    // 50 wide is not divisible by 30 - should fail
    pb_raster_config_t bad_config = {
        .width = 50,
        .height = 1,
        .board = 0,
        .start_string = 0,
        .start_pixel = 0,
        .wrap_mode = PB_WRAP_CHAIN,
        .chain_length = 30,
    };

    int id = pb_raster_create(driver, &bad_config);
    ASSERT_TRUE(id < 0);  // Should fail validation

    // 50 wide is divisible by 25 - should succeed
    pb_raster_config_t good_config = {
        .width = 50,
        .height = 1,
        .board = 0,
        .start_string = 0,
        .start_pixel = 0,
        .wrap_mode = PB_WRAP_CHAIN,
        .chain_length = 25,
    };

    id = pb_raster_create(driver, &good_config);
    ASSERT_TRUE(id >= 0);  // Should succeed

    pb_driver_deinit(driver);
}

// ============================================================================
// Main test runner
// ============================================================================

int main(void) {
    printf("\n=== pb_led_driver tests ===\n\n");

    printf("Color utility tests:\n");
    RUN_TEST(color_rgb_creates_correct_value);
    RUN_TEST(color_rgb_zero);
    RUN_TEST(color_rgb_white);
    RUN_TEST(color_component_extraction);
    RUN_TEST(color_scale_full);
    RUN_TEST(color_scale_half);
    RUN_TEST(color_scale_zero);
    RUN_TEST(color_blend_all_c1);
    RUN_TEST(color_blend_all_c2);
    RUN_TEST(color_blend_halfway);
    RUN_TEST(color_hsv_red);
    RUN_TEST(color_hsv_green);
    RUN_TEST(color_hsv_blue);
    RUN_TEST(color_hsv_white);
    RUN_TEST(color_hsv_black);

    printf("\nDriver init tests:\n");
    RUN_TEST(driver_init_returns_valid_pointer);
    RUN_TEST(driver_get_config_returns_correct_values);

    printf("\nBit-plane encoding tests:\n");
    RUN_TEST(set_pixel_basic);
    RUN_TEST(set_pixel_different_strings);
    RUN_TEST(set_pixel_different_positions);
    RUN_TEST(clear_board_sets_all_pixels);

    printf("\nRaster creation tests:\n");
    RUN_TEST(raster_create_returns_valid_id);
    RUN_TEST(raster_get_dimensions);
    RUN_TEST(raster_set_get_pixel);
    RUN_TEST(raster_fill);

    printf("\nRaster mapping tests:\n");
    RUN_TEST(raster_show_continuous_mapping);
    RUN_TEST(raster_show_zigzag_mapping);
    RUN_TEST(raster_multiple_rasters);
    RUN_TEST(raster_destroy_frees_slot);
    RUN_TEST(raster_zigzag_multi_fold_multi_string);
    RUN_TEST(raster_with_start_offset);
    RUN_TEST(raster_chain_mode_basic);
    RUN_TEST(raster_chain_mode_validation);

    printf("\n=== Results: %d/%d tests passed ===\n\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
