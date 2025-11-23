#include "flash_info.h"
#include "config.h"
#include "spi_ops.h"
#include "mqtt.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "json.h"
#include "lwip/dns.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "pico/mutex.h"
#include "pico/stdlib.h"
#include "sd_card.h"
#include "web_server.h"

// Includes for new modules
#include "globals.h"
#include "flash_ops.h"
#include "spi_diag.h"
#include "cli.h"

#include <stdio.h>
#include <string.h>

// ========== Global Variable Definitions ==========
// These are declared 'extern' in globals.h

char json_buffer[JSON_BUFFER_SIZE];
char pico_ip_address[16] = "0.0.0.0";
bool sd_ready = false;
bool spi_initialized = false;
uint8_t last_jedec_id[3] = {0xFF, 0xFF, 0xFF};

mutex_t spi_mutex;
mutex_t buffer_mutex;

// ========== Main Application ==========
int main(void) {
    stdio_init_all();
    sleep_ms(3000);

    printf("\n");
    printf("╔════════════════════════════════════════╗\n");
    printf("║      SPI Flash Diagnostic Tool v3      ║\n");
    printf("║                                        ║\n");
    printf("╚════════════════════════════════════════╝\n");
    printf("\n");

    // Initialize SPI
    printf("--- Initializing SPI ---\n");
    mutex_init(&spi_mutex);
    mutex_init(&buffer_mutex);
    spi_master_init();
    spi_initialized = true;
    printf("✓ SPI initialized\n");

    // Initialize SD Card
    sd_ready = sd_full_init(); 
    if (!sd_ready) {
        printf("⚠️  Running without SD card\n");
    }

    // Initialize WiFi
    printf("\n--- Initializing WiFi ---\n");
    if (cyw43_arch_init()) {
        printf("✗ WiFi init failed\n");
        return 1;
    }

    cyw43_arch_enable_sta_mode();
    printf("📡 Connecting to %s...\n", WIFI_SSID);

    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD,
                                           CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("✗ WiFi connection failed\n");
        return 1;
    }

    printf("✓ WiFi connected\n");
    const char *ip = ip4addr_ntoa(netif_ip_addr4(netif_default));
    strncpy(pico_ip_address, ip, sizeof(pico_ip_address) - 1);
    printf("✓ IP: %s\n", pico_ip_address);
    
    // Start HTTP server
    http_server_init(pico_ip_address);
    
    // Initialize MQTT
    mqtt_init(); 

    printf("\n========== SYSTEM READY ==========\n");
    printf("✅ SPI: Ready\n");
    printf("✅ WiFi: %s\n", pico_ip_address);
    printf("✅ Web GUI: http://%s\n", pico_ip_address);
    printf("✅ MQTT: %s\n", mqtt_is_connected() ? "Connected" : "Initializing"); 
    printf("✅ SD Card: %s\n", sd_ready ? "Ready" : "Not available");
    printf("==================================\n\n");

    // Main loop
    uint32_t last_status = 0;

    multicore_launch_core1(cli_core);

    while (true) {
        cyw43_arch_poll();
        sleep_ms(10);

        uint32_t now = to_ms_since_boot(get_absolute_time());

        // Periodic status update
        if (now - last_status >= 60000) {
            printf("\n--- System Status ---\n");
            printf("Uptime: %lu seconds\n", now / 1000);
            printf("WiFi: %s\n", pico_ip_address);
            printf("MQTT: %s\n", mqtt_is_connected() ? "Connected" : "Disconnected"); 
            printf("Last JEDEC: %02X %02X %02X\n", last_jedec_id[0], last_jedec_id[1], last_jedec_id[2]);
            last_status = now;
        }

        // LED heartbeat
        static uint32_t last_blink = 0;
        if (now - last_blink >= 1000) {
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
            sleep_ms(50);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
            last_blink = now;
        }
    }

    return 0;
}