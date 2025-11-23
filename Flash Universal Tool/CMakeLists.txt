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


project(main C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

include_directories(include)
include_directories(lib/fatfs)

pico_sdk_init()



# ============================
#  Main executable
# ============================
add_executable(main
    src/main.c 
    src/spi_ops.c 
    src/json.c    
    src/flash_db.c
    src/sd_card.c
    src/web_server.c
    src/mqtt.c
    lib/fatfs/ff.c
    lib/fatfs/diskio.c
    lib/fatfs/ffsystem.c
    lib/fatfs/ffunicode.c)

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
