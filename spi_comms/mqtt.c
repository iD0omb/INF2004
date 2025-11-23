#include "mqtt.h"
#include "config.h" 
#include "lwip/apps/mqtt.h"
#include "lwip/dns.h"
#include <string.h>
#include <stdio.h>

// Internal state
static mqtt_client_t *mqtt_client;
static ip_addr_t mqtt_broker_ip;
static volatile bool mqtt_connected = false;

// Callback: Connection Status
static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status) {
    if (status == MQTT_CONNECT_ACCEPTED) {
        printf("✓ MQTT Connected!\n");
        mqtt_connected = true;
    } else {
        printf("✗ MQTT connection failed (Status: %d)\n", status);
        mqtt_connected = false;
    }
}

// Callback: DNS Found
static void mqtt_dns_found(const char *hostname, const ip_addr_t *ipaddr, void *arg) {
    if (ipaddr != NULL) {
        mqtt_broker_ip = *ipaddr;
        printf("✓ DNS resolved %s to %s\n", hostname, ip4addr_ntoa(ipaddr));

        struct mqtt_connect_client_info_t ci;
        memset(&ci, 0, sizeof(ci));
        
        // You might want to move "pico_spi_flash_tool" to config.h eventually
        ci.client_id = "pico_spi_flash_tool_v2"; 
        ci.keep_alive = 60;

        mqtt_client_connect(mqtt_client, &mqtt_broker_ip, MQTT_PORT,
                            mqtt_connection_cb, NULL, &ci);
    } else {
        printf("✗ DNS failed for %s\n", hostname);
    }
}

// Public: Initialize
void mqtt_init(void) {
    printf("\n--- Initializing MQTT ---\n");
    mqtt_client = mqtt_client_new();
    
    if (!mqtt_client) {
        printf("✗ Failed to create MQTT client struct\n");
        return;
    }

    // Start DNS resolution
    err_t err = dns_gethostbyname(MQTT_BROKER, &mqtt_broker_ip, mqtt_dns_found, NULL);
    
    if (err == ERR_OK) {
        // IP was already in cache, call callback immediately
        mqtt_dns_found(MQTT_BROKER, &mqtt_broker_ip, NULL);
    } else if (err != ERR_INPROGRESS) {
        printf("✗ DNS request failed\n");
    }
}

// Public: Publish
bool mqtt_publish_report(const char *json_data) {
    if (!mqtt_connected || !mqtt_client) {
        return false;
    }

    size_t data_len = strlen(json_data);
    
    // MQTT buffer limitation safety
    if (data_len > 4096) {
        printf("⚠️ Truncating MQTT message to 4096 bytes\n");
        data_len = 4096;
    }

    err_t err = mqtt_publish(mqtt_client, MQTT_TOPIC, json_data, data_len, 
                             0, 0, NULL, NULL); // QoS 0, Retain 0

    if (err == ERR_OK) {
        printf("✓ Published report (%d bytes)\n", (int)data_len);
        return true;
    } else {
        printf("✗ MQTT Publish failed (Err: %d)\n", err);
        return false;
    }
}

// Public: Status Getter
bool mqtt_is_connected(void) {
    return mqtt_connected;
}