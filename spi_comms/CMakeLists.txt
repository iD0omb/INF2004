cmake_minimum_required(VERSION 3.13)

# Import Pico SDK
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(spi_flash_webgui C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# ==========================================================
#  FatFS Library (you already have local no-OS-FatFS project)
# ==========================================================
add_subdirectory(no-OS-FatFS-SD-SPI-RPi-Pico/FatFs_SPI build)

# ============================
#  Main executable
# ============================
add_executable(spi_flash_webgui
    integrated_spi_webgui.c
    functions.c 
    json.c
    hw_config.c
    flash_db.c
)

# ============================
#  Include directories
# ============================
target_include_directories(spi_flash_webgui PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/no-OS-FatFS-SD-SPI-RPi-Pico/FatFs_SPI
)

# ==========================================================
#  Libraries required by your integrated SIP + Web + MQTT
# ==========================================================
target_link_libraries(spi_flash_webgui
    pico_stdlib
    hardware_spi
    hardware_gpio
    pico_multicore
    pico_lwip_mqtt
    pico_lwip_http 
    # --- WiFi stack + LWIP + MQTT + TCP ---
    pico_cyw43_arch_lwip_threadsafe_background  # REQUIRED for webserver + MQTT

    # --- FatFS storage ---
    FatFs_SPI
)

# ============================
#  Enable USB output
# ============================
pico_enable_stdio_usb(spi_flash_webgui 1)
pico_enable_stdio_uart(spi_flash_webgui 0)

# ============================
#  Generate .uf2 firmware
# ============================
pico_add_extra_outputs(spi_flash_webgui)
