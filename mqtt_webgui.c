#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/apps/mqtt.h"
#include "lwip/dns.h"
#include "ff.h"
#include "hw_config.h"

// ========== Configuration ==========
#define WIFI_SSID "Nice"
#define WIFI_PASSWORD "84885247"
#define MQTT_BROKER "broker.hivemq.com"
#define MQTT_PORT 1883
#define MQTT_TOPIC "sit/se33/flash/report"
#define HTTP_PORT 80
#define JSON_BUFFER_SIZE 4096
#define HTML_BUFFER_SIZE 8192

// ========== Global Variables ==========
static mqtt_client_t *mqtt_client;
static ip_addr_t mqtt_broker_ip;
static bool mqtt_connected = false;
static char json_buffer[JSON_BUFFER_SIZE];
static char html_buffer[HTML_BUFFER_SIZE];
static FATFS fs;
static bool sd_ready = false;
static struct tcp_pcb *http_server_pcb;
static char pico_ip_address[16] = "0.0.0.0";

// ========== SD Card Functions ==========

bool init_sd_card() {
    printf("\n========== SD CARD INITIALIZATION ==========\n");
    
    FRESULT fr = f_mount(&fs, "0:", 1);
    if (fr != FR_OK) {
        printf("✗ Failed to mount SD card (Error: %d)\n", fr);
        printf("  Possible reasons:\n");
        printf("  - SD card not inserted\n");
        printf("  - Not formatted as FAT32\n");
        printf("  - Wiring incorrect (check GPIO pins)\n");
        return false;
    }
    
    printf("✓ SD card mounted successfully!\n");
    
    // List all files on SD card
    DIR dir;
    FILINFO fno;
    fr = f_opendir(&dir, "0:/");
    if (fr == FR_OK) {
        printf("\n📁 Files on SD card:\n");
        printf("%-30s %10s\n", "Filename", "Size");
        printf("----------------------------------------\n");
        
        while (true) {
            fr = f_readdir(&dir, &fno);
            if (fr != FR_OK || fno.fname[0] == 0) break;
            printf("%-30s %10lu bytes\n", fno.fname, fno.fsize);
        }
        f_closedir(&dir);
        printf("----------------------------------------\n");
    }
    
    return true;
}

const char* read_json_from_sd() {
    if (!sd_ready) {
        snprintf(json_buffer, sizeof(json_buffer), 
                "{\"error\":\"SD card not initialized\",\"timestamp\":\"%lu\"}", 
                to_ms_since_boot(get_absolute_time()));
        return json_buffer;
    }
    
    FIL fil;
    FRESULT fr;
    UINT bytes_read = 0;
    
    printf("\n--- Reading report.json from SD card ---\n");
    
    // Open the JSON file
    fr = f_open(&fil, "0:/report.json", FA_READ);
    if (fr != FR_OK) {
        printf("✗ Failed to open report.json (Error: %d)\n", fr);
        snprintf(json_buffer, sizeof(json_buffer), 
                "{\"error\":\"File not found: report.json\",\"error_code\":%d}", fr);
        return json_buffer;
    }
    
    // DON'T use f_size() - it's returning garbage
    // Just read directly into buffer with max size
    
    // Read the file into buffer
    fr = f_read(&fil, json_buffer, sizeof(json_buffer) - 1, &bytes_read);
    if (fr != FR_OK || bytes_read == 0) {
        printf("✗ Failed to read file (Error: %d)\n", fr);
        f_close(&fil);
        snprintf(json_buffer, sizeof(json_buffer), 
                "{\"error\":\"Read failed\",\"error_code\":%d}", fr);
        return json_buffer;
    }
    
    // Null-terminate the string
    json_buffer[bytes_read] = '\0';
    
    // Close the file
    f_close(&fil);
    
    printf("✓ Successfully read %u bytes\n", bytes_read);
    printf("📄 Content preview: %.80s%s\n", 
           json_buffer, bytes_read > 80 ? "..." : "");
    
    return json_buffer;
}


// ========== MQTT Functions ==========

static void mqtt_connection_cb(mqtt_client_t *client, void *arg, 
                               mqtt_connection_status_t status) {
    if (status == MQTT_CONNECT_ACCEPTED) {
        printf("\n✓ MQTT Connected to broker!\n");
        mqtt_connected = true;
        
        // Publish the SD card report immediately upon connection
        const char *report = read_json_from_sd();
        err_t err = mqtt_publish(client, MQTT_TOPIC, report, strlen(report), 
                                0, 0, NULL, NULL);
        
        if (err == ERR_OK) {
            printf("✓ Initial report published to topic: %s\n", MQTT_TOPIC);
            printf("📦 Payload size: %d bytes\n", strlen(report));
        } else {
            printf("✗ Initial publish failed (Error: %d)\n", err);
        }
    } else {
        printf("✗ MQTT connection failed (Status: %d)\n", status);
        mqtt_connected = false;
    }
}

static void mqtt_dns_found(const char *hostname, const ip_addr_t *ipaddr, void *arg) {
    if (ipaddr != NULL) {
        mqtt_broker_ip = *ipaddr;
        printf("✓ DNS resolved %s to %s\n", hostname, ip4addr_ntoa(ipaddr));
        
        // Connect to MQTT broker
        struct mqtt_connect_client_info_t ci;
        memset(&ci, 0, sizeof(ci));
        ci.client_id = "pico_se33_mqtt";
        ci.keep_alive = 60;
        
        err_t err = mqtt_client_connect(mqtt_client, &mqtt_broker_ip, MQTT_PORT,
                                       mqtt_connection_cb, NULL, &ci);
        if (err != ERR_OK) {
            printf("✗ MQTT connect call failed (Error: %d)\n", err);
        }
    } else {
        printf("✗ DNS lookup failed for %s\n", hostname);
    }
}

void mqtt_init() {
    printf("\n--- Initializing MQTT ---\n");
    
    mqtt_client = mqtt_client_new();
    if (!mqtt_client) {
        printf("✗ Failed to create MQTT client\n");
        return;
    }
    
    printf("🔍 Resolving MQTT broker: %s\n", MQTT_BROKER);
    err_t err = dns_gethostbyname(MQTT_BROKER, &mqtt_broker_ip, 
                                  mqtt_dns_found, NULL);
    
    if (err == ERR_OK) {
        // DNS already cached
        mqtt_dns_found(MQTT_BROKER, &mqtt_broker_ip, NULL);
    } else if (err == ERR_INPROGRESS) {
        printf("⏳ DNS lookup in progress...\n");
    } else {
        printf("✗ DNS lookup error (Error: %d)\n", err);
    }
}

void mqtt_publish_report(const char *json_data) {
    if (!mqtt_connected || !mqtt_client) {
        printf("⚠ MQTT not connected, skipping publish\n");
        return;
    }
    
    err_t err = mqtt_publish(mqtt_client, MQTT_TOPIC, json_data, 
                            strlen(json_data), 0, 0, NULL, NULL);
    
    if (err == ERR_OK) {
        printf("✓ Published report (%d bytes)\n", strlen(json_data));
    } else {
        printf("✗ Publish failed (Error: %d)\n", err);
    }
}

// ========== HTTP Web Server Functions ==========

const char* generate_html_page() {
    const char *report = read_json_from_sd();
    
    snprintf(html_buffer, sizeof(html_buffer),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n\r\n"
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "  <title>SE33 Flash Diagnostic</title>\n"
        "  <meta name='viewport' content='width=device-width, initial-scale=1.0'>\n"
        "  <style>\n"
        "    body{font-family:Arial;margin:20px;background:#f5f5f5;}\n"
        "    .box{background:white;padding:20px;border-radius:8px;max-width:800px;margin:auto;}\n"
        "    h1{color:#c3002f;}\n"
        "    pre{background:#f4f4f4;padding:15px;border-radius:5px;overflow-x:auto;font-size:0.9em;}\n"
        "    .btn{display:inline-block;padding:10px 20px;background:#0066cc;color:white;text-decoration:none;border-radius:5px;margin:10px 5px 0 0;}\n"
        "  </style>\n"
        "</head>\n"
        "<body>\n"
        "  <div class='box'>\n"
        "    <h1>SE33 Flash Diagnostic Report</h1>\n"
        "    <p><strong>IP:</strong> %s | <strong>MQTT:</strong> %s | <strong>SD Card:</strong> %s</p>\n"
        "    <h3>Report:</h3>\n"
        "    <pre>%s</pre>\n"
        "    <a href='/' class='btn'>Refresh</a>\n"
        "    <a href='/download' class='btn'>Download</a>\n"
        "  </div>\n"
        "</body>\n"
        "</html>",
        pico_ip_address,
        mqtt_connected ? "Connected" : "Disconnected",
        sd_ready ? "Ready" : "Not Ready",
        report
    );
    
    return html_buffer;
}


static err_t http_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    if (p == NULL) {
        tcp_close(pcb);
        return ERR_OK;
    }
    
    // Get request data
    char request[512];
    size_t len = p->tot_len < sizeof(request) - 1 ? p->tot_len : sizeof(request) - 1;
    pbuf_copy_partial(p, request, len, 0);
    request[len] = '\0';
    
    printf("\n--- HTTP Request Received ---\n");
    printf("From: %s\n", ip4addr_ntoa(&pcb->remote_ip));
    
    // Check if download request
    if (strstr(request, "GET /download") != NULL) {
        printf("Request: Download JSON\n");
        
        const char *json_data = read_json_from_sd();
        size_t json_len = strlen(json_data);
        
        // Send HTTP headers for JSON download
        char headers[256];
        snprintf(headers, sizeof(headers),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Disposition: attachment; filename=\"flash_report_%lu.json\"\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n",
            to_ms_since_boot(get_absolute_time()),
            json_len
        );
        
        tcp_write(pcb, headers, strlen(headers), TCP_WRITE_FLAG_COPY);
        tcp_write(pcb, json_data, json_len, TCP_WRITE_FLAG_COPY);
        tcp_output(pcb);
        
        printf("✓ Sent JSON file for download\n");
    } else {
        // Serve HTML page
        printf("Request: Webpage\n");
        
        const char *html = generate_html_page();
        tcp_write(pcb, html, strlen(html), TCP_WRITE_FLAG_COPY);
        tcp_output(pcb);
        
        printf("✓ Served HTML webpage\n");
    }
    
    // Clean up
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    tcp_close(pcb);
    
    return ERR_OK;
}

static err_t http_accept(void *arg, struct tcp_pcb *newpcb, err_t err) {
    printf("📡 HTTP client connected: %s\n", ip4addr_ntoa(&newpcb->remote_ip));
    tcp_recv(newpcb, http_recv);
    return ERR_OK;
}

void http_server_init() {
    printf("\n--- Starting HTTP Server ---\n");
    
    http_server_pcb = tcp_new();
    if (http_server_pcb == NULL) {
        printf("✗ Failed to create HTTP server PCB\n");
        return;
    }
    
    err_t err = tcp_bind(http_server_pcb, IP_ADDR_ANY, HTTP_PORT);
    if (err != ERR_OK) {
        printf("✗ Failed to bind to port %d (Error: %d)\n", HTTP_PORT, err);
        return;
    }
    
    http_server_pcb = tcp_listen(http_server_pcb);
    tcp_accept(http_server_pcb, http_accept);
    
    printf("✓ HTTP server listening on port %d\n", HTTP_PORT);
    printf("🌐 Access web interface at: http://%s\n", pico_ip_address);
}

// ========== Main Program ==========

int main() {
    stdio_init_all();
    sleep_ms(2000);
    
    printf("\n");
    printf("╔════════════════════════════════════════════════════════╗\n");
    printf("║      SE33 Flash Diagnostic System v1.0                ║\n");
    printf("║      MQTT + Web GUI + SD Card Integration             ║\n");
    printf("║      Singapore Institute of Technology                ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    // Initialize SD card
    sd_ready = init_sd_card();
    
    if (!sd_ready) {
        printf("\n⚠️  WARNING: Running without SD card\n");
        printf("    System will continue but won't have real data\n");
    }
    
    // Initialize WiFi
    printf("\n========== WIFI INITIALIZATION ==========\n");
    if (cyw43_arch_init_with_country(CYW43_COUNTRY_SINGAPORE)) {
        printf("✗ Failed to initialize WiFi hardware\n");
        return 1;
    }
    
    cyw43_arch_enable_sta_mode();
    printf("📡 Connecting to WiFi SSID: %s\n", WIFI_SSID);
    printf("⏳ Please wait...\n");
    
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD,
                                           CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("✗ WiFi connection failed\n");
        printf("   Check: SSID correct? Password correct? 2.4GHz network?\n");
        return 1;
    }
    
    printf("✓ WiFi connected successfully!\n");
    const char *ip = ip4addr_ntoa(netif_ip_addr4(netif_default));
    strncpy(pico_ip_address, ip, sizeof(pico_ip_address) - 1);
    printf("✓ IP Address: %s\n", pico_ip_address);
    
    // Initialize MQTT
    mqtt_init();
    
    // Initialize HTTP server
    http_server_init();
    
    printf("\n========== SYSTEM READY ==========\n");
    printf("✅ SD Card: %s\n", sd_ready ? "Ready" : "Not Available");
    printf("✅ WiFi: Connected (%s)\n", pico_ip_address);
    printf("✅ MQTT: Initializing...\n");
    printf("✅ Web Server: http://%s\n", pico_ip_address);
    printf("\n🚀 System operational - Publishing every 30 seconds\n");
    printf("📊 Open web browser to view reports in real-time\n");
    printf("==================================\n\n");
    
    // Main loop
    uint32_t last_publish = 0;
    uint32_t last_status = 0;
    
    while (true) {
        // Process WiFi/network events
        cyw43_arch_poll();
        sleep_ms(10);
        
        uint32_t now = to_ms_since_boot(get_absolute_time());
        
        // Publish to MQTT every 30 seconds
        if (mqtt_connected && sd_ready && (now - last_publish >= 30000)) {
            printf("\n========== PUBLISHING REPORT ==========\n");
            const char *report = read_json_from_sd();
            mqtt_publish_report(report);
            last_publish = now;
            
            // Blink LED on successful publish
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
            sleep_ms(200);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
            sleep_ms(100);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
            sleep_ms(200);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        }
        
        // Print status every 60 seconds
        if (now - last_status >= 60000) {
            printf("\n--- System Status ---\n");
            printf("Uptime: %lu seconds\n", now / 1000);
            printf("MQTT: %s\n", mqtt_connected ? "Connected" : "Disconnected");
            printf("SD Card: %s\n", sd_ready ? "Ready" : "Not Ready");
            printf("IP: %s\n", pico_ip_address);
            last_status = now;
        }
    }
    
    return 0;
}
