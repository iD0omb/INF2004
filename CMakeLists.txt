add_executable(wifi_exchange
    wifi_exchange.c
)

# Include current directory for lwipopts.h
target_include_directories(wifi_exchange PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(wifi_exchange
    pico_stdlib
    pico_cyw43_arch_lwip_threadsafe_background
)

pico_enable_stdio_usb(wifi_exchange 1)
pico_enable_stdio_uart(wifi_exchange 0)

pico_add_extra_outputs(wifi_exchange)
