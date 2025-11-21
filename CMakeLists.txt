cmake_minimum_required(VERSION 3.13)

# Set board type to Pico W before project declaration
set(PICO_BOARD pico_w)

project(microsd C CXX ASM)

add_executable(microsd
    main.c
    flash_benchmark.c
    sd_card.c
    fatfs/ff.c
    fatfs/diskio.c
    fatfs/ffsystem.c
    fatfs/ffunicode.c
)

# Include directories
target_include_directories(microsd PRIVATE 
    ${CMAKE_CURRENT_LIST_DIR}                              # Current directory (for sd_card.h and main.c)
    ${CMAKE_CURRENT_LIST_DIR}/fatfs                        # FatFs library
) 

# Link libraries
target_link_libraries(microsd
    pico_stdlib
    hardware_gpio
    hardware_adc
    hardware_spi
)

# Enable USB and UART for stdio
pico_enable_stdio_usb(microsd 1)
pico_enable_stdio_uart(microsd 1)
pico_add_extra_outputs(microsd)
