# Generated Cmake Pico project file
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
set(PICO_BOARD pixel_blit CACHE STRING "Board type")
cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

set(PICO_PLATFORM rp2350)

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(pixel_blit_firmware C CXX ASM)
  set(PICO_RP2350A 0 CACHE STRING "RP2350B variant" FORCE)
# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add pb_led_driver library
add_subdirectory(lib/pb_led_driver)

# Add executable. Default name is the project name, version 0.1

add_executable(pixel_blit_firmware
        src/main.c
        src/reducer.c
        src/side_effects.c
        src/views.c
        sh1106.c
        string_test.c
        toggle_test.c
        rainbow_test.c)

pico_generate_pio_header(pixel_blit_firmware ${CMAKE_CURRENT_LIST_DIR}/string_test.pio)

pico_set_program_name(pixel_blit_firmware "pixel_blit_firmware")
pico_set_program_version(pixel_blit_firmware "0.1")

pico_enable_stdio_uart(pixel_blit_firmware 0)
pico_enable_stdio_usb(pixel_blit_firmware 1)

target_link_libraries(pixel_blit_firmware
        pico_stdlib
        pico_multicore
        hardware_i2c
        hardware_adc
        hardware_pio
        hardware_clocks
        pb_led_driver)

target_include_directories(pixel_blit_firmware PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/src
        ${CMAKE_CURRENT_LIST_DIR}/lib/pb_led_driver)

add_compile_definitions( PICO_RP2350A=0)
target_compile_definitions(pixel_blit_firmware PRIVATE
        PICO_DEFAULT_USB_VBUS_PIN=-1
        PICO_RP2350A=0)

# Configure for SRAM-only execution (no flash initialization)
#pico_set_binary_type(pixel_blit_firmware no_flash)


pico_add_extra_outputs(pixel_blit_firmware)
