cmake_minimum_required(VERSION 3.13)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(spi_comms C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(spi_comms main.c functions.c)
target_include_directories(spi_comms PRIVATE .)
target_link_libraries(spi_comms functions pico_stdlib hardware_spi hardware_gpio pico_stdio pico_time)
pico_enable_stdio_usb(spi_comms 1)
pico_enable_stdio_uart(spi_comms 0)
pico_add_extra_outputs(spi_comms)



