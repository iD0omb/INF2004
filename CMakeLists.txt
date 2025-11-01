# Path to FatFS library
set(FATFS_DIR ${CMAKE_CURRENT_LIST_DIR}/../../no-OS-FatFS-SD-SPI-RPi-Pico)

add_executable(mqtt_webgui
    mqtt_webgui.c
    hw_config.c
)

# Include FatFS library
add_subdirectory(${FATFS_DIR}/FatFs_SPI build)

target_include_directories(mqtt_webgui PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${FATFS_DIR}/FatFs_SPI
)

target_link_libraries(mqtt_webgui
    pico_stdlib
    pico_cyw43_arch_lwip_threadsafe_background
    pico_lwip_mqtt
    pico_lwip_http           # ADD THIS - needed for HTTP/TCP web server
    FatFs_SPI
    hardware_spi
)

pico_enable_stdio_usb(mqtt_webgui 1)
pico_enable_stdio_uart(mqtt_webgui 0)

pico_add_extra_outputs(mqtt_webgui)
