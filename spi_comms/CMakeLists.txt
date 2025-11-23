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
set(PICO_BOARD pico_w CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.13)

# 1. Initialize SDK
include(pico_sdk_import.cmake)

# This name can be anything. It does not need to be "spi_comms".
project(main C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()



# ============================
#  Main executable
# ============================
# The first word here becomes your output filename (spi_flash_webgui.uf2)
add_executable(main
    main.c 
    spi_ops.c 
    json.c    
    flash_db.c
    sd_card.c
    fatfs/ff.c
    fatfs/diskio.c
    fatfs/ffsystem.c
    fatfs/ffunicode.c)

# ============================
#  Include directories
# ============================
target_include_directories(main PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/fatfs
)

# ============================
#  Libraries
# ============================
# This name MUST match the add_executable name above
target_link_libraries(main
    pico_stdlib
    hardware_spi
    hardware_gpio
    hardware_adc
    pico_multicore
    pico_lwip_mqtt
    pico_lwip_http 
    pico_cyw43_arch_lwip_threadsafe_background
)

# ============================
#  Enable USB output
# ============================
pico_enable_stdio_usb(main 1)
pico_enable_stdio_uart(main 0)

# ============================
#  Generate .uf2 firmware
# ============================
pico_add_extra_outputs(main)
