#include "web_server.h"
#include "flash_info.h"
#include "config.h"
#include "spi_ops.h" // Ensures we can call SPI functions
#include "mqtt.h" // Ensures we can check MQTT status
#include "sd_card.h"
#include "json.h"

#include "lwip/tcp.h"
#include "lwip/pbuf.h"
#include "pico/stdlib.h"
#include "pico/mutex.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ========== External Globals ==========
// We need these from main.c (or wherever they live now)
extern char json_buffer[JSON_BUFFER_SIZE];
extern bool sd_ready;
extern bool spi_initialized;
extern uint8_t last_jedec_id[3];
extern mutex_t buffer_mutex;
extern mutex_t spi_mutex;

// We also need access to the diagnostic functions. 
// Ideally, these should be in spi_ops.h, but if they are in main.c, 
// they must be non-static.
extern bool run_spi_diagnostic(char *json_out, size_t json_cap);
extern bool read_jedec_id(uint8_t *mfr, uint8_t *mem_type, uint8_t *capacity);

typedef struct http_connection {
    struct tcp_pcb *pcb;
    bool in_use;
    uint32_t timestamp;
} http_connection_t;

static http_connection_t http_connections[MAX_HTTP_CONNECTIONS];
static struct tcp_pcb *http_server_pcb;
static char server_ip[16]; // Local copy of IP for display

// ========== Helper Functions ==========

void cleanup_old_connections(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    for (int i = 0; i < MAX_HTTP_CONNECTIONS; i++) {
        if (http_connections[i].in_use) {
            if (now - http_connections[i].timestamp > 10000) {
                if (http_connections[i].pcb) {
                    tcp_abort(http_connections[i].pcb);
                }
                http_connections[i].in_use = false;
            }
        }
    }
}

int register_connection(struct tcp_pcb *pcb) {
    cleanup_old_connections();
    for (int i = 0; i < MAX_HTTP_CONNECTIONS; i++) {
        if (!http_connections[i].in_use) {
            http_connections[i].pcb = pcb;
            http_connections[i].in_use = true;
            http_connections[i].timestamp = to_ms_since_boot(get_absolute_time());
            return i;
        }
    }
    return -1;
}

void unregister_connection(struct tcp_pcb *pcb) {
    for (int i = 0; i < MAX_HTTP_CONNECTIONS; i++) {
        if (http_connections[i].pcb == pcb) {
            http_connections[i].in_use = false;
            http_connections[i].pcb = NULL;
            break;
        }
    }
}

void generate_html_page(char *output, size_t size) {
    char chip_info[128] = "Not scanned";
    if (last_jedec_id[0] != 0xFF) {
        snprintf(chip_info, sizeof(chip_info),
                 "MFR: 0x%02X | Type: 0x%02X | Cap: 0x%02X", 
                 last_jedec_id[0], last_jedec_id[1], last_jedec_id[2]);
    }
    snprintf(
      output, size,
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/html\r\n"
      "Connection: close\r\n\r\n"
      "<!DOCTYPE html>\n"
      "<html>\n"
      "<head>\n"
      "  <meta charset='utf-8'>\n"
      "  <meta name='viewport' content='width=device-width, initial-scale=1'>\n"
      "  <title>SPI Flash Diagnostics</title>\n"
      "  <style>\n"
      "    * { margin: 0; padding: 0; box-sizing: border-box; }\n"
      "    body { font-family: system-ui, sans-serif; background: #0f172a; "
      "color: #e2e8f0; padding: 20px; }\n"
      "    .container { max-width: 1200px; margin: 0 auto; }\n"
      "    .header { background: linear-gradient(135deg, #3b82f6 0%%, #8b5cf6 "
      "100%%); padding: 30px; border-radius: 12px; margin-bottom: 20px; }\n"
      "    h1 { font-size: 28px; margin-bottom: 10px; }\n"
      "    .status { font-size: 14px; opacity: 0.9; }\n"
      "    .card { background: #1e293b; padding: 25px; border-radius: 12px; "
      "margin-bottom: 20px; }\n"
      "    .card h2 { color: #60a5fa; margin-bottom: 15px; }\n"
      "    .btn { padding: 12px 24px; background: #3b82f6; color: white; "
      "border: none; border-radius: 8px; cursor: pointer; font-size: 14px; "
      "font-weight: 500; }\n"
      "    .btn:hover { background: #2563eb; }\n"
      "    .btn:disabled { background: #475569; cursor: not-allowed; }\n"
      "    .btn-group { display: flex; gap: 10px; flex-wrap: wrap; }\n"
      "    pre { background: #0f172a; padding: 20px; border-radius: 8px;"
      "white-space: pre-wrap; word-wrap: break-word; "
      "overflow-x: auto; font-size: 14px; max-height: 80vh;min-height: 300px ;overflow-y: auto; "
      "}\n"
      "    .info { color: #94a3b8; font-size: 14px; margin-top: 10px; }\n"
      "    .loading { display: none; color: #60a5fa; }\n"
      "    .loading.active { display: inline; }\n"
      "  </style>\n"
      "</head>\n"
      "<body>\n"
      "  <div class='container'>\n"
      "    <div class='header'>\n"
      "      <h1>SPI Flash Diagnostic Tool</h1>\n"
      "      <div class='status'>IP: %s | SPI: %s | SD: %s | MQTT: %s</div>\n"
      "    </div>\n"
      "    <div class='card'>\n"
      "      <h2>Quick Identification</h2>\n"
      "      <div class='btn-group'>\n"
      "        <button class='btn' onclick='scanJedec()'>Read JEDEC "
      "ID</button>\n"
      "        <span class='loading' id='jedecLoading'>Reading...</span>\n"
      "      </div>\n"
      "      <div class='info' id='jedecInfo'>%s</div>\n"
      "    </div>\n"
      "    <div class='card'>\n"
      "      <h2>Full Diagnostic Report</h2>\n"
      "      <div class='btn-group'>\n"
      "        <button class='btn' onclick='runFullScan()'>Run Full "
      "Scan</button>\n"
      "        <button class='btn' onclick='downloadReport()'>Download "
      "JSON</button>\n"
      "        <button class='btn' onclick='publishMqtt()' %s>Publish via "
      "MQTT</button>\n"
      "        <span class='loading' id='scanLoading'>Scanning...</span>\n"
      "      </div>\n"
      "      <pre id='reportData'>Click \"Run Full Scan\" to begin...</pre>\n"
      "    </div>\n"
      "    <div class='card'>\n"
      "      <h2>Saved Reports</h2>\n"
      "      <div class='btn-group'>\n"
      "        <button class='btn' onclick='viewReport(\"latest.jsn\")'>View "
      "Latest</button>\n"
      "      </div>\n"
      "      <div class='info'>Reports are automatically saved to SD "
      "card</div>\n"
      "    </div>\n"
      "  </div>\n"
      "  <script>\n"
      "    async function scanJedec() {\n"
      "      document.getElementById('jedecLoading').classList.add('active');\n"
      "      const resp = await fetch('/api/jedec');\n"
      "      const data = await resp.json();\n"
      "      "
      "document.getElementById('jedecLoading').classList.remove('active');\n"
      "      if (data.error) {\n"
      "        document.getElementById('jedecInfo').textContent = 'Error: ' + "
      "data.error;\n"
      "      } else {\n"
      "        document.getElementById('jedecInfo').textContent = \n"
      "          `Manufacturer: 0x${data.manufacturer} | Memory Type: "
      "0x${data.memory_type} | Capacity: 0x${data.capacity}`;\n"
      "      }\n"
      "    }\n"
      "    async function runFullScan() {\n"
      "      document.getElementById('scanLoading').classList.add('active');\n"
      "      document.getElementById('reportData').textContent = 'Scanning "
      "flash memory...';\n"
      "      const resp = await fetch('/api/scan');\n"
      "      const data = await resp.text();\n"
      "      "
      "document.getElementById('scanLoading').classList.remove('active');\n"
      "      document.getElementById('reportData').textContent = data;\n"
      "    }\n"
      "    async function downloadReport() {\n"
      "      window.location.href = '/api/download';\n"
      "    }\n"
      "    async function publishMqtt() {\n"
      "      const resp = await fetch('/api/publish');\n"
      "      const data = await resp.json();\n"
      "      alert(data.message || data.error);\n"
      "    }\n"
      "    async function viewReport(filename) {\n"
      "      const resp = await fetch(`/api/view?file=${filename}`);\n"
      "      const data = await resp.text();\n"
      "      document.getElementById('reportData').textContent = data;\n"
      "    }\n"
      "  </script>\n"
      "</body>\n"
      "</html>",
      server_ip, spi_initialized ? "Ready" : "Not Init",
      sd_ready ? "Ready" : "No Card", mqtt_is_connected() ? "Connected" : "Offline",
      chip_info, mqtt_is_connected() ? "" : "disabled");
}

// ========== Request Handler ==========

static err_t http_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    if (p == NULL) {
        unregister_connection(pcb);
        tcp_close(pcb);
        return ERR_OK;
    }

    // Allocate buffers
    char *request = malloc(p->tot_len + 1);
    char *response = malloc(HTML_BUFFER_SIZE);

    if (!request || !response) {
        if (request) free(request);
        if (response) free(response);
        pbuf_free(p);
        tcp_abort(pcb);
        return ERR_ABRT;
    }

    pbuf_copy_partial(p, request, p->tot_len, 0);
    request[p->tot_len] = '\0';

    // ================== ROUTING LOGIC ==================

    // 1. Root / Index
    if (strstr(request, "GET / ") || strstr(request, "GET /index")) {
        generate_html_page(response, HTML_BUFFER_SIZE);
        tcp_write(pcb, response, strlen(response), TCP_WRITE_FLAG_COPY);
    
    // 2. JEDEC ID
    } else if (strstr(request, "GET /api/jedec")) {
        uint8_t mfr, mem_type, capacity;
        if (read_jedec_id(&mfr, &mem_type, &capacity)) {
            snprintf(response, HTML_BUFFER_SIZE,
                     "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n"
                     "{\"manufacturer\":\"%02X\",\"memory_type\":\"%02X\",\"capacity\":\"%02X\"}",
                     mfr, mem_type, capacity);
        } else {
             snprintf(response, HTML_BUFFER_SIZE, "HTTP/1.1 500 Internal Server Error\r\n\r\n{\"error\":\"Read Failed\"}");
        }
        tcp_write(pcb, response, strlen(response), TCP_WRITE_FLAG_COPY);

    // 3. Full Scan
    } else if (strstr(request, "GET /api/scan")) {
        mutex_enter_blocking(&buffer_mutex);
        bool success = run_spi_diagnostic(json_buffer, JSON_BUFFER_SIZE);
        
        // Save to SD card if successful
        if (success && sd_ready) {
            sd_write_safe("latest.jsn", json_buffer);
        }
        
        snprintf(response, HTML_BUFFER_SIZE, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n%s", json_buffer);
        mutex_exit(&buffer_mutex);
        tcp_write(pcb, response, strlen(response), TCP_WRITE_FLAG_COPY);

    // 4. Download JSON (This was missing!)
    } else if (strstr(request, "GET /api/download")) {
        mutex_enter_blocking(&buffer_mutex);
        
        // Attempt to read the latest report
        bool file_read = false;
        if (sd_ready) {
            // Assuming sd_read_safe returns true on success
            file_read = sd_read_safe("latest.jsn", json_buffer, JSON_BUFFER_SIZE);
        }

        if (file_read) {
            // Write headers first
            int header_len = snprintf(response, HTML_BUFFER_SIZE,
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: application/json\r\n"
                     "Content-Disposition: attachment; filename=\"report.json\"\r\n"
                     "Connection: close\r\n\r\n");
            
            tcp_write(pcb, response, header_len, TCP_WRITE_FLAG_COPY);
            // Write body directly from json_buffer to avoid stack overflow in response buffer
            tcp_write(pcb, json_buffer, strlen(json_buffer), TCP_WRITE_FLAG_COPY);
        } else {
            snprintf(response, HTML_BUFFER_SIZE, "HTTP/1.1 404 Not Found\r\n\r\nFile not found. Run a scan first.");
            tcp_write(pcb, response, strlen(response), TCP_WRITE_FLAG_COPY);
        }
        mutex_exit(&buffer_mutex);

    // 5. Publish MQTT (Fixed headers and file check)
    } else if (strstr(request, "GET /api/publish")) {
        if (mqtt_is_connected()) {
            mutex_enter_blocking(&buffer_mutex);
            
            bool file_read = false;
            if (sd_ready) {
                file_read = sd_read_safe("latest.jsn", json_buffer, JSON_BUFFER_SIZE);
            }

            if (file_read) {
                mqtt_publish_report(json_buffer);
                snprintf(response, HTML_BUFFER_SIZE, 
                        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\"message\":\"Published\"}");
            } else {
                snprintf(response, HTML_BUFFER_SIZE, 
                        "HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\n\r\n{\"error\":\"No report file found on SD\"}");
            }
            mutex_exit(&buffer_mutex);
        } else {
            snprintf(response, HTML_BUFFER_SIZE, 
                    "HTTP/1.1 503 Service Unavailable\r\nContent-Type: application/json\r\n\r\n{\"error\":\"MQTT Not Connected\"}");
        }
        tcp_write(pcb, response, strlen(response), TCP_WRITE_FLAG_COPY);

    // 6. View Report (New Handler)
    } else if (strstr(request, "GET /api/view")) {
        char *file_param = strstr(request, "file=");
        bool file_found = false;

        if (file_param) {
            file_param += 5; // Skip "file="
            char *end = strpbrk(file_param, " \r\n");
            if (end) *end = '\0';
            
            // Basic safety check to prevent directory traversal
            if (!strstr(file_param, "..") && sd_ready) {
                mutex_enter_blocking(&buffer_mutex);
                file_found = sd_read_safe(file_param, json_buffer, JSON_BUFFER_SIZE);
                if (file_found) {
                     snprintf(response, HTML_BUFFER_SIZE, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n%s", json_buffer);
                }
                mutex_exit(&buffer_mutex);
            }
        }
        
        if (!file_found) {
            snprintf(response, HTML_BUFFER_SIZE, "HTTP/1.1 404 Not Found\r\n\r\n{\"error\":\"File not found\"}");
        }
        tcp_write(pcb, response, strlen(response), TCP_WRITE_FLAG_COPY);
    }

    // ===================================================

    tcp_output(pcb);
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    free(request);
    free(response);

    unregister_connection(pcb);
    tcp_close(pcb);
    return ERR_OK;
}

static err_t http_accept(void *arg, struct tcp_pcb *newpcb, err_t err) {
    if (err != ERR_OK || newpcb == NULL) return ERR_VAL;
    
    int slot = register_connection(newpcb);
    if (slot < 0) {
        tcp_abort(newpcb);
        return ERR_ABRT;
    }
    
    tcp_arg(newpcb, newpcb);
    tcp_recv(newpcb, http_recv);
    return ERR_OK;
}

void http_server_init(const char *ip_address) {
    printf("\n--- Starting HTTP Server ---\n");
    strncpy(server_ip, ip_address, sizeof(server_ip)-1);
    
    memset(http_connections, 0, sizeof(http_connections));
    // Note: Mutexes should be initialized in main.c before this is called

    http_server_pcb = tcp_new();
    if (!http_server_pcb) return;

    if (tcp_bind(http_server_pcb, IP_ADDR_ANY, HTTP_PORT) != ERR_OK) return;

    http_server_pcb = tcp_listen(http_server_pcb);
    tcp_accept(http_server_pcb, http_accept);

    printf("✓ HTTP Server running at http://%s\n", server_ip);
}
