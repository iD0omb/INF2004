#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"

// WiFi credentials
#define WIFI_SSID "Nice"
#define WIFI_PASSWORD "84885247"

// UDP configuration
#define UDP_PORT 1234
#define BEACON_MSG_LEN_MAX 128

// Global variables
static struct udp_pcb *udp_rx_pcb;
static struct udp_pcb *udp_tx_pcb;
static char received_data[BEACON_MSG_LEN_MAX + 1];
static volatile bool new_message = false;

// UDP receive callback function
static void udp_recv_callback(void *arg, struct udp_pcb *upcb, struct pbuf *p,
                              const ip_addr_t *addr, u16_t port) {
    if (p != NULL) {
        // Copy the received data
        memcpy(received_data, p->payload, p->len);
        received_data[p->len] = '\0'; // Null terminate
        
        printf("Received from PC: %s\n", received_data);
        new_message = true;
        
        // Free the packet buffer
        pbuf_free(p);
        
        // Send response back to PC
        struct pbuf *tx_buf = pbuf_alloc(PBUF_TRANSPORT, BEACON_MSG_LEN_MAX, PBUF_RAM);
        if (tx_buf != NULL) {
            char *response = (char *)tx_buf->payload;
            snprintf(response, BEACON_MSG_LEN_MAX, "Pico received: %s", received_data);
            
            // Send UDP packet back
            err_t err = udp_sendto(udp_tx_pcb, tx_buf, addr, port);
            if (err == ERR_OK) {
                printf("Sent response to PC\n");
            } else {
                printf("Failed to send response, error: %d\n", err);
            }
            
            pbuf_free(tx_buf);
        }
    }
}

// Initialize UDP receive
int udp_receive_init(void) {
    // Create new UDP PCB
    udp_rx_pcb = udp_new();
    if (udp_rx_pcb == NULL) {
        printf("Failed to create UDP PCB\n");
        return -1;
    }
    
    // Bind to local port
    err_t err = udp_bind(udp_rx_pcb, IP_ADDR_ANY, UDP_PORT);
    if (err != ERR_OK) {
        printf("Failed to bind UDP port, error: %d\n", err);
        return -1;
    }
    
    // Setup receive callback
    udp_recv(udp_rx_pcb, udp_recv_callback, NULL);
    printf("UDP receiver initialized on port %d\n", UDP_PORT);
    
    return 0;
}

// Initialize UDP transmit
int udp_transmit_init(void) {
    udp_tx_pcb = udp_new();
    if (udp_tx_pcb == NULL) {
        printf("Failed to create UDP TX PCB\n");
        return -1;
    }
    printf("UDP transmitter initialized\n");
    return 0;
}

int main() {
    stdio_init_all();
    sleep_ms(2000); // Wait for USB serial
    
    printf("Pico W WiFi Data Exchange Demo\n");
    
    // Initialize WiFi with country code
    if (cyw43_arch_init_with_country(CYW43_COUNTRY_SINGAPORE)) {
        printf("Failed to initialize WiFi\n");
        return 1;
    }
    
    cyw43_arch_enable_sta_mode();
    printf("Connecting to WiFi...\n");
    
    // Connect to WiFi
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, 
                                           CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("Failed to connect to WiFi\n");
        return 1;
    }
    
    printf("Connected to WiFi\n");
    printf("IP Address: %s\n", ip4addr_ntoa(netif_ip_addr4(netif_default)));
    
    // Initialize UDP
    if (udp_receive_init() != 0) {
        printf("Failed to initialize UDP receiver\n");
        return 1;
    }
    
    if (udp_transmit_init() != 0) {
        printf("Failed to initialize UDP transmitter\n");
        return 1;
    }
    
    printf("Waiting for UDP packets on port %d...\n", UDP_PORT);
    
    // Main loop
    while (true) {
        // Let the WiFi stack process
        cyw43_arch_poll();
        sleep_ms(10);
        
        // Blink LED if message received
        if (new_message) {
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
            sleep_ms(100);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
            new_message = false;
        }
    }
    
    cyw43_arch_deinit();
    return 0;
}
