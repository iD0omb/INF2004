cmake_minimum_required(VERSION 3.13)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pico-pico C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(master main.c functions.c)
target_include_directories(master PRIVATE .)
target_link_libraries(master pico_stdlib hardware_spi)
pico_enable_stdio_usb(master 1)
pico_enable_stdio_uart(master 0)
pico_add_extra_outputs(master)

add_executable(slave slave.c functions.c)
target_include_directories(slave PRIVATE .)
target_link_libraries(slave pico_stdlib hardware_spi)
pico_enable_stdio_usb(slave 1)
pico_enable_stdio_uart(slave 0)
pico_add_extra_outputs(slave)



