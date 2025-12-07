# pb_led_driver library CMakeLists.txt

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(PB_LED_DRIVER_TEST "Build tests for pb_led_driver" OFF)

if(PB_LED_DRIVER_TEST)
    # Host-based test build (no Pico SDK)
    project(pb_led_driver_test C)

    add_executable(pb_led_driver_test
        test_pb_led_driver.c
        pb_led_driver.c
        pb_led_driver_color.c
        pb_led_driver_raster.c
    )

    target_compile_definitions(pb_led_driver_test PRIVATE
        PB_LED_DRIVER_TEST_BUILD=1
    )

    target_include_directories(pb_led_driver_test PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
    )
else()
    # Pico SDK library build
    add_library(pb_led_driver INTERFACE)

    target_sources(pb_led_driver INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/pb_led_driver.c
        ${CMAKE_CURRENT_LIST_DIR}/pb_led_driver_color.c
        ${CMAKE_CURRENT_LIST_DIR}/pb_led_driver_raster.c
        ${CMAKE_CURRENT_LIST_DIR}/pb_led_driver_hw.c
    )

    target_include_directories(pb_led_driver INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
    )

    # Generate PIO header from .pio file
    pico_generate_pio_header(pb_led_driver ${CMAKE_CURRENT_LIST_DIR}/ws2811_parallel.pio)

    target_link_libraries(pb_led_driver INTERFACE
        pico_stdlib
        hardware_pio
        hardware_dma
        hardware_clocks
        pico_sync
    )
endif()
