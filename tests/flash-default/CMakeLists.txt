cmake_minimum_required(VERSION 3.21)
set(PICO_BOARD pico2 CACHE STRING "SDK board")
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "SDK platform")
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(flash_default C CXX ASM)
set(CMAKE_C_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
pico_sdk_init()
option(TEST_EMBEDDED_W25 "Force the pico2 board's W25 boot2; NOT the SDK default" OFF)
add_executable(flash_default main.c)
target_compile_definitions(flash_default PRIVATE
    TEST_EMBEDDED_W25=$<BOOL:${TEST_EMBEDDED_W25}>
    PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=0
    PICO_STDIO_USB_STDOUT_TIMEOUT_US=1000
    PICO_STDIO_USB_ENABLE_RESET_VIA_BAUD_RATE=1)
if(TEST_EMBEDDED_W25)
    target_compile_definitions(flash_default PRIVATE PICO_EMBED_XIP_SETUP=1)
    set_target_properties(flash_default PROPERTIES OUTPUT_NAME flash-forced-embedded-w25)
else()
    # No XIP, boot2, pad, flash-divider, voltage, or system-clock override.
    set_target_properties(flash_default PROPERTIES OUTPUT_NAME flash-sdk-default)
endif()
target_compile_options(flash_default PRIVATE -O2 -g3)
target_link_libraries(flash_default PRIVATE pico_stdlib hardware_clocks)
pico_enable_stdio_usb(flash_default 1)
pico_enable_stdio_uart(flash_default 0)
pico_add_extra_outputs(flash_default)
