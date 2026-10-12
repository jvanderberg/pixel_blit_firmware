cmake_minimum_required(VERSION 3.13)

set(PICO_SDK_PATH $ENV{HOME}/.pico-sdk/sdk/2.1.1)
set(PICO_PLATFORM rp2350-arm-s)

include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

project(slower_boot2 C ASM)
pico_sdk_init()

# Build slower boot2 with CLKDIV=4
pico_define_boot_stage2(slower_boot2 ${PICO_SDK_PATH}/src/rp2350/boot_stage2/boot2_w25q080.S)
target_compile_definitions(slower_boot2 PRIVATE PICO_FLASH_SPI_CLKDIV=4)
