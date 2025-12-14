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

# =============================================================================
# Fetch Dependencies
# =============================================================================

include(FetchContent)

# SD Card / FatFS Driver
FetchContent_Declare(
  no-OS-FatFS-SD-SPI-RPi-Pico
  GIT_REPOSITORY https://github.com/carlk3/no-OS-FatFS-SD-SPI-RPi-Pico.git
)
FetchContent_MakeAvailable(no-OS-FatFS-SD-SPI-RPi-Pico)

# =============================================================================
# Libraries
# =============================================================================

# Add pb_led_driver library
add_subdirectory(lib/pb_led_driver)

# Add fseq_parser library
add_subdirectory(lib/fseq_parser)

# =============================================================================
# Main Executable
# =============================================================================

add_executable(pixel_blit_firmware
        src/main.c
        src/reducer.c
        src/side_effects.c
        src/views.c
        src/sd_ops.c
        src/board_config.c
        src/flash_settings.c
        sh1106.c
        string_test.c
        toggle_test.c
        rainbow_test.c
        string_length_test.c
        fseq_player.c
        ir_control.c
        lib/sd_card/hw_config.c
        
        # FatFS / SD Card Driver Sources
        ${no-os-fatfs-sd-spi-rpi-pico_SOURCE_DIR}/FatFs_SPI/sd_driver/sd_card.c
        ${no-os-fatfs-sd-spi-rpi-pico_SOURCE_DIR}/FatFs_SPI/sd_driver/sd_spi.c
        ${no-os-fatfs-sd-spi-rpi-pico_SOURCE_DIR}/FatFs_SPI/sd_driver/spi.c
        ${no-os-fatfs-sd-spi-rpi-pico_SOURCE_DIR}/FatFs_SPI/sd_driver/crc.c
        ${no-os-fatfs-sd-spi-rpi-pico_SOURCE_DIR}/FatFs_SPI/ff15/source/ff.c
        ${no-os-fatfs-sd-spi-rpi-pico_SOURCE_DIR}/FatFs_SPI/ff15/source/ffsystem.c
        ${no-os-fatfs-sd-spi-rpi-pico_SOURCE_DIR}/FatFs_SPI/ff15/source/ffunicode.c
        # Glue code
        ${no-os-fatfs-sd-spi-rpi-pico_SOURCE_DIR}/FatFs_SPI/src/glue.c
        lib/sd_card/my_rtc.c
)

pico_generate_pio_header(pixel_blit_firmware ${CMAKE_CURRENT_LIST_DIR}/string_test.pio)

pico_set_program_name(pixel_blit_firmware "pixel_blit_firmware")
pico_set_program_version(pixel_blit_firmware "0.1")

pico_enable_stdio_uart(pixel_blit_firmware 0)
pico_enable_stdio_usb(pixel_blit_firmware 1)

target_link_libraries(pixel_blit_firmware
        pico_stdlib
        pico_multicore
        pico_flash
        hardware_i2c
        hardware_adc
        hardware_spi
        hardware_pio
        hardware_clocks
        hardware_flash
        pb_led_driver
        fseq_parser
)

target_include_directories(pixel_blit_firmware PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/src
        ${CMAKE_CURRENT_LIST_DIR}/lib/pb_led_driver
        ${CMAKE_CURRENT_LIST_DIR}/lib/fseq_parser/include
        ${CMAKE_CURRENT_LIST_DIR}/lib/sd_card
        ${no-os-fatfs-sd-spi-rpi-pico_SOURCE_DIR}/FatFs_SPI/sd_driver
        ${no-os-fatfs-sd-spi-rpi-pico_SOURCE_DIR}/FatFs_SPI/include
        ${no-os-fatfs-sd-spi-rpi-pico_SOURCE_DIR}/FatFs_SPI/ff15/source
)

add_compile_definitions( PICO_RP2350A=0)
target_compile_definitions(pixel_blit_firmware PRIVATE
        PICO_DEFAULT_USB_VBUS_PIN=-1
        PICO_RP2350A=0)

# Configure for SRAM-only execution (no flash initialization)
#pico_set_binary_type(pixel_blit_firmware no_flash)


pico_add_extra_outputs(pixel_blit_firmware)