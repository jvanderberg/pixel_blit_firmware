cmake_minimum_required(VERSION 3.13)
project(test_board_config C)

set(CMAKE_C_STANDARD 11)

# Define test build flag
add_compile_definitions(BOARD_CONFIG_TEST_BUILD)

# We need pb_led_driver.h for the color order enum
# Create a minimal stub for the types we need
add_executable(test_board_config
    test_board_config.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/board_config.c
    pb_led_driver_stub.c
)

target_include_directories(test_board_config PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${CMAKE_CURRENT_SOURCE_DIR}
)
