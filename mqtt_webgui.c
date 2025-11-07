#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/apps/mqtt.h"
#include "lwip/dns.h"
#include "ff.h"
#include "hw_config.h"
#include "pico/mutex.h"

// ========== Configuration ==========
#define WIFI_SSID "Nice"
#define WIFI_PASSWORD "84885247"
#define MQTT_BROKER "broker.hivemq.com"
#undef MQTT_PORT
#define MQTT_PORT 1883
#define MQTT_TOPIC "sit/se33/flash/report"
#define HTTP_PORT 80
#define JSON_BUFFER_SIZE 4096
#define HTML_BUFFER_SIZE 12288
#define MAX_HTTP_CONNECTIONS 3
#define MAX_FILENAME_LEN 64
#define MAX_UPLOAD_SIZE (100 * 1024)  // 100KB max

// ========== Global Variables ==========
static mqtt_client_t *mqtt_client;
static ip_addr_t mqtt_broker_ip;
static bool mqtt_connected = false;
static char json_buffer[JSON_BUFFER_SIZE];
static FATFS fs;
static bool sd_ready = false;
static struct tcp_pcb *http_server_pcb;
static char pico_ip_address[16] = "0.0.0.0";
static char current_json_file[MAX_FILENAME_LEN] = "report.json";

// Connection tracking - WITH upload support
typedef struct http_connection {
    struct tcp_pcb *pcb;
    bool in_use;
    uint32_t timestamp;
    char *upload_buffer;
    size_t upload_size;
    size_t upload_received;
} http_connection_t;

static http_connection_t http_connections[MAX_HTTP_CONNECTIONS];
static mutex_t sd_mutex;
static mutex_t buffer_mutex;

// ========== Utility Functions ==========
void cleanup_old_connections() {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    for (int i = 0; i < MAX_HTTP_CONNECTIONS; i++) {
        if (http_connections[i].in_use) {
            if (now - http_connections[i].timestamp > 10000) {
                printf("⚠ Cleaning up stale connection %d\n", i);
                if (http_connections[i].pcb) {
                    tcp_abort(http_connections[i].pcb);
                }
                if (http_connections[i].upload_buffer) {
                    free(http_connections[i].upload_buffer);
                }
                http_connections[i].in_use = false;
                http_connections[i].pcb = NULL;
                http_connections[i].upload_buffer = NULL;
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
            http_connections[i].upload_buffer = NULL;
            http_connections[i].upload_size = 0;
            http_connections[i].upload_received = 0;
            return i;
        }
    }
    return -1;
}

void unregister_connection(struct tcp_pcb *pcb) {
    for (int i = 0; i < MAX_HTTP_CONNECTIONS; i++) {
        if (http_connections[i].pcb == pcb) {
            if (http_connections[i].upload_buffer) {
                free(http_connections[i].upload_buffer);
            }
            http_connections[i].in_use = false;
            http_connections[i].pcb = NULL;
            http_connections[i].upload_buffer = NULL;
            break;
        }
    }
}

void url_decode(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if ((*src == '%') && ((a = src[1]) && (b = src[2])) && 
            (isxdigit(a) && isxdigit(b))) {
            if (a >= 'a') a -= 'a'-'A';
            if (a >= 'A') a -= ('A' - 10);
            else a -= '0';
            if (b >= 'a') b -= 'a'-'A';
            if (b >= 'A') b -= ('A' - 10);
            else b -= '0';
            *dst++ = 16*a+b;
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

// ========== SD Card Functions ==========
bool init_sd_card() {
    printf("\n========== SD CARD INITIALIZATION ==========\n");
    mutex_init(&sd_mutex);
    
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

int list_json_files(char *output_buffer, size_t buffer_size) {
    if (!sd_ready) {
        snprintf(output_buffer, buffer_size, "[]");
        return 0;
    }

    mutex_enter_blocking(&sd_mutex);
    
    DIR dir;
    FILINFO fno;
    FRESULT fr = f_opendir(&dir, "0:/");
    
    if (fr != FR_OK) {
        mutex_exit(&sd_mutex);
        snprintf(output_buffer, buffer_size, "[]");
        return 0;
    }

    int count = 0;
    int pos = snprintf(output_buffer, buffer_size, "[");
    
    while (true) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == 0) break;
        
        size_t len = strlen(fno.fname);
        if (len > 5 && strcmp(&fno.fname[len-5], ".json") == 0) {
            if (count > 0) {
                pos += snprintf(output_buffer + pos, buffer_size - pos, ",");
            }
            pos += snprintf(output_buffer + pos, buffer_size - pos, 
                          "{\"name\":\"%s\",\"size\":%lu}", 
                          fno.fname, fno.fsize);
            count++;
        }
    }
    
    pos += snprintf(output_buffer + pos, buffer_size - pos, "]");
    f_closedir(&dir);
    mutex_exit(&sd_mutex);
    
    return count;
}

bool read_json_file_safe(const char *filename, char *output_buffer, size_t buffer_size) {
    if (!sd_ready) {
        snprintf(output_buffer, buffer_size,
                "{\"error\":\"SD card not initialized\",\"timestamp\":\"%lu\"}",
                to_ms_since_boot(get_absolute_time()));
        return false;
    }

    mutex_enter_blocking(&sd_mutex);

    FIL fil;
    FRESULT fr;
    UINT bytes_read = 0;
    char filepath[MAX_FILENAME_LEN + 4];
    
    snprintf(filepath, sizeof(filepath), "0:/%s", filename);

    fr = f_open(&fil, filepath, FA_READ);
    if (fr != FR_OK) {
        mutex_exit(&sd_mutex);
        snprintf(output_buffer, buffer_size,
                "{\"error\":\"File not found: %s\",\"error_code\":%d}", filename, fr);
        return false;
    }

    fr = f_read(&fil, output_buffer, buffer_size - 1, &bytes_read);
    f_close(&fil);
    
    mutex_exit(&sd_mutex);

    if (fr != FR_OK || bytes_read == 0) {
        snprintf(output_buffer, buffer_size,
                "{\"error\":\"Read failed\",\"error_code\":%d}", fr);
        return false;
    }

    output_buffer[bytes_read] = '\0';
    return true;
}

const char* read_json_from_sd() {
    mutex_enter_blocking(&buffer_mutex);
    read_json_file_safe(current_json_file, json_buffer, JSON_BUFFER_SIZE);
    mutex_exit(&buffer_mutex);
    return json_buffer;
}

// NEW: Write file to SD card
bool write_file_to_sd(const char *filename, const char *data, size_t size) {
    if (!sd_ready) {
        printf("✗ SD card not ready\n");
        return false;
    }

    mutex_enter_blocking(&sd_mutex);

    FIL fil;
    FRESULT fr;
    UINT bytes_written = 0;
    char filepath[MAX_FILENAME_LEN + 4];
    
    snprintf(filepath, sizeof(filepath), "0:/%s", filename);

    fr = f_open(&fil, filepath, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) {
        mutex_exit(&sd_mutex);
        printf("✗ Failed to create file: %s (Error: %d)\n", filename, fr);
        return false;
    }

    fr = f_write(&fil, data, size, &bytes_written);
    f_close(&fil);
    
    mutex_exit(&sd_mutex);

    if (fr != FR_OK || bytes_written != size) {
        printf("✗ Failed to write file (Error: %d)\n", fr);
        return false;
    }

    printf("✓ File uploaded: %s (%u bytes)\n", filename, bytes_written);
    return true;
}

// ========== MQTT Functions ==========
static void mqtt_connection_cb(mqtt_client_t *client, void *arg,
                               mqtt_connection_status_t status) {
    if (status == MQTT_CONNECT_ACCEPTED) {
        printf("\n✓ MQTT Connected to broker!\n");
        mqtt_connected = true;
        
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

    size_t data_len = strlen(json_data);
    if (data_len > 2048) {
        printf("⚠ JSON data too large (%d bytes), truncating to 2048\n", data_len);
        data_len = 2048;
    }

    err_t err = mqtt_publish(mqtt_client, MQTT_TOPIC, json_data,
                            data_len, 0, 0, NULL, NULL);
    if (err == ERR_OK) {
        printf("✓ Published report (%d bytes)\n", data_len);
    } else if (err == ERR_MEM) {
        printf("✗ Publish failed: Out of memory (Error: %d)\n", err);
        printf("  Try reducing JSON file size or wait for buffers to clear\n");
    } else if (err == ERR_CONN) {
        printf("✗ Publish failed: Not connected (Error: %d)\n", err);
        mqtt_connected = false;
    } else {
        printf("✗ Publish failed (Error: %d)\n", err);
    }
}

// NEW: Parse multipart form data
bool parse_multipart_upload(const char *data, size_t data_len, const char *boundary,
                           char *filename, size_t filename_size,
                           const char **file_data, size_t *file_size) {
    
    printf("📝 Parsing upload (len: %d, boundary: '%s')\n", data_len, boundary);
    
    // Find Content-Disposition header
    const char *disposition = strstr(data, "Content-Disposition");
    if (!disposition) {
        printf("✗ No Content-Disposition found\n");
        return false;
    }
    
    // Extract filename
    const char *fname_start = strstr(disposition, "filename=\"");
    if (!fname_start) {
        printf("✗ No filename field found\n");
        return false;
    }
    fname_start += 10; // Skip 'filename="'
    
    const char *fname_end = strchr(fname_start, '"');
    if (!fname_end) {
        printf("✗ No closing quote for filename\n");
        return false;
    }
    
    size_t fname_len = fname_end - fname_start;
    if (fname_len >= filename_size) fname_len = filename_size - 1;
    if (fname_len == 0) {
        printf("✗ Empty filename\n");
        return false;
    }
    
    strncpy(filename, fname_start, fname_len);
    filename[fname_len] = '\0';
    printf("📄 Filename: '%s'\n", filename);
    
    // Find file data start (after double CRLF or double LF)
    const char *data_start = strstr(fname_end, "\r\n\r\n");
    if (!data_start) {
        // Try LF only (some browsers)
        data_start = strstr(fname_end, "\n\n");
        if (!data_start) {
            printf("✗ No data start marker found\n");
            return false;
        }
        data_start += 2; // Skip \n\n
    } else {
        data_start += 4; // Skip \r\n\r\n
    }
    
    // Find end boundary - try multiple formats
    char end_boundary[128];
    const char *data_end = NULL;
    
    // Format 1: \r\n--boundary
    snprintf(end_boundary, sizeof(end_boundary), "\r\n--%s", boundary);
    data_end = strstr(data_start, end_boundary);
    
    // Format 2: \n--boundary (LF only)
    if (!data_end) {
        snprintf(end_boundary, sizeof(end_boundary), "\n--%s", boundary);
        data_end = strstr(data_start, end_boundary);
    }
    
    // Format 3: --boundary at end (no leading CRLF)
    if (!data_end) {
        snprintf(end_boundary, sizeof(end_boundary), "--%s", boundary);
        data_end = strstr(data_start, end_boundary);
    }
    
    if (!data_end) {
        printf("✗ No end boundary found\n");
        return false;
    }
    
    *file_data = data_start;
    *file_size = data_end - data_start;
    
    printf("✓ File data: %d bytes\n", *file_size);
    return true;
}


// ========== HTTP Web Server Functions ==========
bool generate_html_page_safe(char *output_buffer, size_t buffer_size) {
    char local_json[JSON_BUFFER_SIZE];
    char file_list[1024];
    
    list_json_files(file_list, sizeof(file_list));
    
    if (!read_json_file_safe(current_json_file, local_json, sizeof(local_json))) {
        snprintf(local_json, sizeof(local_json), "{\"error\":\"Failed to read JSON\"}");
    }
    
    int written = snprintf(output_buffer, buffer_size,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-cache, no-store, must-revalidate\r\n"
        "Pragma: no-cache\r\n"
        "Expires: 0\r\n"
        "\r\n"
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "  <meta charset=\"UTF-8\">\n"
        "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
        "  <title>SE33 Flash Diagnostic</title>\n"
        "  <style>\n"
        "    * { margin: 0; padding: 0; box-sizing: border-box; }\n"
        "    body { font-family: 'Segoe UI', system-ui, sans-serif; background: #0f172a; color: #e2e8f0; padding: 20px; }\n"
        "    .container { max-width: 1200px; margin: 0 auto; }\n"
        "    .header { background: linear-gradient(135deg, #1e40af 0%%, #3b82f6 100%%); padding: 30px; border-radius: 12px; margin-bottom: 20px; box-shadow: 0 4px 6px rgba(0,0,0,0.3); }\n"
        "    h1 { font-size: 28px; font-weight: 600; margin-bottom: 15px; }\n"
        "    .status { display: flex; gap: 20px; flex-wrap: wrap; font-size: 14px; }\n"
        "    .status-item { background: rgba(255,255,255,0.1); padding: 8px 16px; border-radius: 6px; }\n"
        "    .status-item strong { color: #60a5fa; }\n"
        "    .card { background: #1e293b; padding: 25px; border-radius: 12px; margin-bottom: 20px; box-shadow: 0 2px 4px rgba(0,0,0,0.2); }\n"
        "    .card h2 { font-size: 20px; margin-bottom: 15px; color: #60a5fa; }\n"
        "    .file-selector { margin-bottom: 20px; display: flex; gap: 10px; align-items: center; flex-wrap: wrap; }\n"
        "    .file-selector label { font-weight: 500; color: #94a3b8; }\n"
        "    .file-selector select { padding: 10px 15px; border-radius: 8px; background: #0f172a; color: #e2e8f0; border: 1px solid #334155; font-size: 14px; min-width: 200px; cursor: pointer; }\n"
        "    .file-selector button { padding: 10px 20px; background: #3b82f6; color: white; border: none; border-radius: 8px; font-weight: 500; cursor: pointer; transition: background 0.2s; }\n"
        "    .file-selector button:hover { background: #2563eb; }\n"
        "    .upload-form { margin-top: 20px; padding: 20px; background: #0f172a; border-radius: 8px; border: 2px dashed #334155; }\n"
        "    .upload-form label { display: block; margin-bottom: 10px; color: #94a3b8; font-weight: 500; }\n"
        "    .upload-form input[type=\"file\"] { display: block; width: 100%%; padding: 10px; margin: 10px 0; border-radius: 6px; background: #1e293b; color: #e2e8f0; border: 1px solid #334155; }\n"
        "    .upload-form button { width: 100%%; padding: 12px; background: #10b981; color: white; border: none; border-radius: 8px; font-weight: 500; cursor: pointer; transition: background 0.2s; }\n"
        "    .upload-form button:hover { background: #059669; }\n"
        "    .upload-status { margin-top: 10px; padding: 10px; border-radius: 6px; display: none; }\n"
        "    .upload-status.success { background: rgba(16, 185, 129, 0.2); color: #10b981; display: block; }\n"
        "    .upload-status.error { background: rgba(239, 68, 68, 0.2); color: #ef4444; display: block; }\n"
        "    .loading { display: none; color: #60a5fa; font-size: 14px; }\n"
        "    .loading.active { display: inline-block; }\n"
        "    pre { background: #0f172a; padding: 20px; border-radius: 8px; overflow-x: auto; font-size: 13px; line-height: 1.6; border: 1px solid #334155; max-height: 600px; overflow-y: auto; }\n"
        "    .actions { display: flex; gap: 15px; margin-top: 20px; flex-wrap: wrap; }\n"
        "    .btn { padding: 12px 24px; border-radius: 8px; text-decoration: none; font-weight: 500; display: inline-block; transition: all 0.2s; border: none; cursor: pointer; font-size: 14px; }\n"
        "    .btn-primary { background: #3b82f6; color: white; }\n"
        "    .btn-primary:hover { background: #2563eb; transform: translateY(-2px); box-shadow: 0 4px 8px rgba(59,130,246,0.3); }\n"
        "    .btn-secondary { background: #64748b; color: white; }\n"
        "    .btn-secondary:hover { background: #475569; transform: translateY(-2px); }\n"
        "    .footer { text-align: center; margin-top: 30px; color: #64748b; font-size: 14px; }\n"
        "    @media (max-width: 768px) {\n"
        "      body { padding: 10px; }\n"
        "      .header { padding: 20px; }\n"
        "      h1 { font-size: 24px; }\n"
        "      .status { font-size: 12px; }\n"
        "      pre { font-size: 11px; padding: 15px; }\n"
        "      .file-selector { flex-direction: column; align-items: stretch; }\n"
        "      .file-selector select, .file-selector button { width: 100%%; }\n"
        "    }\n"
        "  </style>\n"
        "</head>\n"
        "<body>\n"
        "  <div class=\"container\">\n"
        "    <div class=\"header\">\n"
        "      <h1>🔥 SE33 Flash Diagnostic Report</h1>\n"
        "      <div class=\"status\">\n"
        "        <div class=\"status-item\"><strong>IP:</strong> %s</div>\n"
        "        <div class=\"status-item\"><strong>MQTT:</strong> %s</div>\n"
        "        <div class=\"status-item\"><strong>SD Card:</strong> %s</div>\n"
        "        <div class=\"status-item\"><strong>Current File:</strong> %s</div>\n"
        "      </div>\n"
        "    </div>\n"
        "    <div class=\"card\">\n"
        "      <h2>📂 Select Report File</h2>\n"
        "      <div class=\"file-selector\">\n"
        "        <label for=\"fileSelect\">JSON File:</label>\n"
        "        <select id=\"fileSelect\">\n"
        "          <option value=\"\">Loading files...</option>\n"
        "        </select>\n"
        "        <button onclick=\"loadSelectedFile()\">Load File</button>\n"
        "        <span class=\"loading\" id=\"loading\">Loading...</span>\n"
        "      </div>\n"
        "    </div>\n"
        "    <div class=\"card\">\n"
        "      <h2>📤 Upload File to SD Card</h2>\n"
        "      <form class=\"upload-form\" id=\"uploadForm\" enctype=\"multipart/form-data\">\n"
        "        <label for=\"fileInput\">Select file to upload (max 100KB):</label>\n"
        "        <input type=\"file\" name=\"file\" id=\"fileInput\" required>\n"
        "        <button type=\"submit\">📤 Upload File</button>\n"
        "      </form>\n"
        "      <div class=\"upload-status\" id=\"uploadStatus\"></div>\n"
        "    </div>\n"
        "    <div class=\"card\">\n"
        "      <h2>📊 Report Data</h2>\n"
        "      <pre id=\"reportData\">%s</pre>\n"
        "      <div class=\"actions\">\n"
        "        <a href=\"/\" class=\"btn btn-primary\">🔄 Refresh</a>\n"
        "        <button onclick=\"downloadFile()\" class=\"btn btn-secondary\">📥 Download</button>\n"
        "      </div>\n"
        "    </div>\n"
        "    <div class=\"footer\">\n"
        "      <p>Singapore Institute of Technology | File Upload Enabled</p>\n"
        "    </div>\n"
        "  </div>\n"
        "  <script>\n"
        "    const jsonFiles = %s;\n"
        "    const currentFile = '%s';\n"
        "    \n"
        "    function populateFileSelect() {\n"
        "      const select = document.getElementById('fileSelect');\n"
        "      select.innerHTML = '';\n"
        "      \n"
        "      if (jsonFiles.length === 0) {\n"
        "        select.innerHTML = '<option value=\"\">No JSON files found</option>';\n"
        "        return;\n"
        "      }\n"
        "      \n"
        "      jsonFiles.forEach(file => {\n"
        "        const option = document.createElement('option');\n"
        "        option.value = file.name;\n"
        "        option.textContent = `${file.name} (${(file.size/1024).toFixed(1)} KB)`;\n"
        "        if (file.name === currentFile) {\n"
        "          option.selected = true;\n"
        "        }\n"
        "        select.appendChild(option);\n"
        "      });\n"
        "    }\n"
        "    \n"
        "    function loadSelectedFile() {\n"
        "      const select = document.getElementById('fileSelect');\n"
        "      const filename = select.value;\n"
        "      if (!filename) return;\n"
        "      \n"
        "      document.getElementById('loading').classList.add('active');\n"
        "      window.location.href = `/load?file=${encodeURIComponent(filename)}`;\n"
        "    }\n"
        "    \n"
        "    function downloadFile() {\n"
        "      const select = document.getElementById('fileSelect');\n"
        "      const filename = select.value || currentFile;\n"
        "      window.location.href = `/download?file=${encodeURIComponent(filename)}`;\n"
        "    }\n"
        "    \n"
        "    document.getElementById('uploadForm').addEventListener('submit', async (e) => {\n"
        "      e.preventDefault();\n"
        "      const fileInput = document.getElementById('fileInput');\n"
        "      const statusDiv = document.getElementById('uploadStatus');\n"
        "      const file = fileInput.files[0];\n"
        "      \n"
        "      if (!file) {\n"
        "        statusDiv.className = 'upload-status error';\n"
        "        statusDiv.textContent = '❌ Please select a file';\n"
        "        return;\n"
        "      }\n"
        "      \n"
        "      if (file.size > 102400) {\n"
        "        statusDiv.className = 'upload-status error';\n"
        "        statusDiv.textContent = '❌ File too large (max 100KB)';\n"
        "        return;\n"
        "      }\n"
        "      \n"
        "      statusDiv.className = 'upload-status';\n"
        "      statusDiv.textContent = '⏳ Uploading...';\n"
        "      statusDiv.style.display = 'block';\n"
        "      \n"
        "      const formData = new FormData();\n"
        "      formData.append('file', file);\n"
        "      \n"
        "      try {\n"
        "        const response = await fetch('/upload', {\n"
        "          method: 'POST',\n"
        "          body: formData\n"
        "        });\n"
        "        \n"
        "        if (response.ok) {\n"
        "          statusDiv.className = 'upload-status success';\n"
        "          statusDiv.textContent = '✅ File uploaded successfully!';\n"
        "          setTimeout(() => window.location.reload(), 1500);\n"
        "        } else {\n"
        "          statusDiv.className = 'upload-status error';\n"
        "          statusDiv.textContent = '❌ Upload failed';\n"
        "        }\n"
        "      } catch (err) {\n"
        "        statusDiv.className = 'upload-status error';\n"
        "        statusDiv.textContent = '❌ Upload error: ' + err.message;\n"
        "      }\n"
        "    });\n"
        "    \n"
        "    populateFileSelect();\n"
        "  </script>\n"
        "</body>\n"
        "</html>",
        pico_ip_address,
        mqtt_connected ? "✅ Connected" : "❌ Disconnected",
        sd_ready ? "✅ Ready" : "❌ Not Ready",
        current_json_file,
        local_json,
        file_list,
        current_json_file
    );
    
    return (written > 0 && written < buffer_size);
}

static void http_err_callback(void *arg, err_t err) {
    struct tcp_pcb *pcb = (struct tcp_pcb *)arg;
    printf("⚠ HTTP connection error (err: %d)\n", err);
    unregister_connection(pcb);
}

// CORRECTED http_recv with proper upload state tracking

static err_t http_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    if (p == NULL) {
        unregister_connection(pcb);
        tcp_close(pcb);
        return ERR_OK;
    }

    // Find connection slot
    int conn_slot = -1;
    for (int i = 0; i < MAX_HTTP_CONNECTIONS; i++) {
        if (http_connections[i].pcb == pcb) {
            conn_slot = i;
            break;
        }
    }

    if (conn_slot < 0) {
        printf("✗ Connection not found\n");
        pbuf_free(p);
        tcp_abort(pcb);
        return ERR_ABRT;
    }

    // Check if we're already in upload mode for this connection
    bool is_upload = (http_connections[conn_slot].upload_buffer != NULL);
    
    if (!is_upload) {
        // Check if this is a NEW upload request
        char header_check[100];
        pbuf_copy_partial(p, header_check, sizeof(header_check) - 1, 0);
        header_check[sizeof(header_check) - 1] = '\0';
        is_upload = (strstr(header_check, "POST /upload") != NULL);
        
        if (is_upload) {
            printf("📤 Starting new upload\n");
        }
    } else {
        printf("📥 Continuing upload\n");
    }
    
    if (is_upload) {
        // Get Content-Length from header (only on first packet)
        size_t content_length = 0;
        
        if (http_connections[conn_slot].upload_buffer == NULL) {
            // First packet - read Content-Length
            char header_check[1000];
            size_t hdr_size = (p->tot_len < sizeof(header_check) - 1) ? p->tot_len : sizeof(header_check) - 1;
            pbuf_copy_partial(p, header_check, hdr_size, 0);
            header_check[hdr_size] = '\0';
            
            const char *cl = strstr(header_check, "Content-Length:");
            if (cl) {
                content_length = atoi(cl + 15);
                printf("📏 Content-Length: %d bytes\n", content_length);
            } else {
                printf("✗ No Content-Length header\n");
                pbuf_free(p);
                unregister_connection(pcb);
                tcp_abort(pcb);
                return ERR_ABRT;
            }

            if (content_length > MAX_UPLOAD_SIZE) {
                printf("✗ Upload too large: %d bytes\n", content_length);
                const char *error_resp = "HTTP/1.1 413 Payload Too Large\r\n\r\n";
                tcp_write(pcb, error_resp, strlen(error_resp), TCP_WRITE_FLAG_COPY);
                tcp_output(pcb);
                pbuf_free(p);
                unregister_connection(pcb);
                tcp_close(pcb);
                return ERR_OK;
            }
            
            // Allocate buffer for entire upload
            http_connections[conn_slot].upload_buffer = (char *)malloc(content_length + 2000);
            if (!http_connections[conn_slot].upload_buffer) {
                printf("✗ Failed to allocate upload buffer\n");
                const char *error_resp = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
                tcp_write(pcb, error_resp, strlen(error_resp), TCP_WRITE_FLAG_COPY);
                tcp_output(pcb);
                pbuf_free(p);
                unregister_connection(pcb);
                tcp_close(pcb);
                return ERR_OK;
            }
            http_connections[conn_slot].upload_size = content_length + 2000;
            http_connections[conn_slot].upload_received = 0;
            printf("✓ Allocated %d byte buffer\n", http_connections[conn_slot].upload_size);
        }

        // Copy data to buffer
        size_t space_left = http_connections[conn_slot].upload_size - http_connections[conn_slot].upload_received;
        size_t to_copy = (p->tot_len < space_left) ? p->tot_len : space_left;
        
        pbuf_copy_partial(p, 
                         http_connections[conn_slot].upload_buffer + http_connections[conn_slot].upload_received,
                         to_copy, 0);
        http_connections[conn_slot].upload_received += to_copy;
        
        printf("📥 Received %d bytes (total: %d)\n", to_copy, http_connections[conn_slot].upload_received);

        tcp_recved(pcb, p->tot_len);
        pbuf_free(p);

        // Check if upload is complete by looking for end boundary
        const char *buf = http_connections[conn_slot].upload_buffer;
        size_t len = http_connections[conn_slot].upload_received;
        
        // Find boundary in Content-Type (search in first 1000 bytes only)
        char boundary[128] = "";
        const char *ct = strstr(buf, "boundary=");
        if (ct && ct < buf + 1000) {
            ct += 9;
            const char *ct_end = strstr(ct, "\r\n");
            if (!ct_end) ct_end = strstr(ct, "\n");
            if (ct_end && (ct_end - ct) < sizeof(boundary)) {
                size_t boundary_len = ct_end - ct;
                strncpy(boundary, ct, boundary_len);
                boundary[boundary_len] = '\0';
            }
        }

        // Check for end boundary marker
        bool upload_complete = false;
        if (strlen(boundary) > 0) {
            char end_marker[140];
            snprintf(end_marker, sizeof(end_marker), "--%s--", boundary);
            if (strstr(buf, end_marker) != NULL) {
                upload_complete = true;
                printf("✅ Found end boundary\n");
            }
        }

        if (upload_complete) {
            printf("✅ Upload complete, processing...\n");
            
            // Parse and save
            char filename[MAX_FILENAME_LEN];
            const char *file_data = NULL;
            size_t file_size = 0;
            
            if (parse_multipart_upload(buf, len, boundary, 
                                     filename, sizeof(filename), 
                                     &file_data, &file_size)) {
                
                if (file_size == 0) {
                    printf("✗ Empty file\n");
                    const char *error_resp = "HTTP/1.1 400 Bad Request\r\n\r\nEmpty file";
                    tcp_write(pcb, error_resp, strlen(error_resp), TCP_WRITE_FLAG_COPY);
                } else if (write_file_to_sd(filename, file_data, file_size)) {
                    printf("✓ Upload SUCCESS: %s (%d bytes)\n", filename, file_size);
                    const char *success_resp = "HTTP/1.1 200 OK\r\n\r\nFile uploaded successfully";
                    tcp_write(pcb, success_resp, strlen(success_resp), TCP_WRITE_FLAG_COPY);
                } else {
                    printf("✗ SD write failed\n");
                    const char *error_resp = "HTTP/1.1 500 Internal Server Error\r\n\r\nSD write failed";
                    tcp_write(pcb, error_resp, strlen(error_resp), TCP_WRITE_FLAG_COPY);
                }
            } else {
                printf("✗ Parse failed\n");
                const char *error_resp = "HTTP/1.1 400 Bad Request\r\n\r\nInvalid data";
                tcp_write(pcb, error_resp, strlen(error_resp), TCP_WRITE_FLAG_COPY);
            }
            
            tcp_output(pcb);
            unregister_connection(pcb);
            tcp_close(pcb);
        }
        // Otherwise, keep connection open and wait for more data
        return ERR_OK;
    }

    // Handle non-upload requests (GET requests)
    char *request = (char *)malloc(p->tot_len + 1);
    char *response_buffer = (char *)malloc(HTML_BUFFER_SIZE);
    
    if (!request || !response_buffer) {
        if (request) free(request);
        if (response_buffer) free(response_buffer);
        pbuf_free(p);
        unregister_connection(pcb);
        tcp_abort(pcb);
        return ERR_ABRT;
    }

    pbuf_copy_partial(p, request, p->tot_len, 0);
    request[p->tot_len] = '\0';

    printf("\n--- HTTP Request ---\n");
    printf("From: %s\n", ip4addr_ntoa(&pcb->remote_ip));

    // Handle /load?file=
    if (strstr(request, "GET /load?file=") != NULL) {
        char *file_param = strstr(request, "file=");
        if (file_param) {
            file_param += 5;
            char *space = strchr(file_param, ' ');
            if (space) *space = '\0';
            
            char decoded_filename[MAX_FILENAME_LEN];
            url_decode(decoded_filename, file_param);
            
            printf("Request: Load file '%s'\n", decoded_filename);
            strncpy(current_json_file, decoded_filename, MAX_FILENAME_LEN - 1);
            const char *redirect = "HTTP/1.1 302 Found\r\nLocation: /\r\n\r\n";
            tcp_write(pcb, redirect, strlen(redirect), TCP_WRITE_FLAG_COPY);
            tcp_output(pcb);
        }
    }
    // Handle download
    else if (strstr(request, "GET /download") != NULL) {
        char filename[MAX_FILENAME_LEN];
        char *file_param = strstr(request, "file=");
        
        if (file_param) {
            file_param += 5;
            char *space = strchr(file_param, ' ');
            if (space) *space = '\0';
            url_decode(filename, file_param);
        } else {
            strncpy(filename, current_json_file, MAX_FILENAME_LEN);
        }
        
        printf("Request: Download '%s'\n", filename);
        
        char local_json[JSON_BUFFER_SIZE];
        read_json_file_safe(filename, local_json, sizeof(local_json));
        
        char headers[256];
        snprintf(headers, sizeof(headers),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json\r\n"
                "Content-Disposition: attachment; filename=\"%s\"\r\n"
                "Connection: close\r\n\r\n", filename);
        
        tcp_write(pcb, headers, strlen(headers), TCP_WRITE_FLAG_COPY);
        tcp_write(pcb, local_json, strlen(local_json), TCP_WRITE_FLAG_COPY);
        tcp_output(pcb);
    }
    // Serve main page
    else {
        printf("Request: Webpage\n");
        if (generate_html_page_safe(response_buffer, HTML_BUFFER_SIZE)) {
            tcp_write(pcb, response_buffer, strlen(response_buffer), TCP_WRITE_FLAG_COPY);
            tcp_output(pcb);
            printf("✓ Served webpage\n");
        }
    }

    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    free(request);
    free(response_buffer);
    
    unregister_connection(pcb);
    err_t close_err = tcp_close(pcb);
    if (close_err != ERR_OK) {
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    
    return ERR_OK;
}

static err_t http_accept(void *arg, struct tcp_pcb *newpcb, err_t err) {
    if (err != ERR_OK || newpcb == NULL) {
        printf("✗ HTTP accept error\n");
        return ERR_VAL;
    }

    int slot = register_connection(newpcb);
    if (slot < 0) {
        printf("⚠ Too many connections, rejecting\n");
        tcp_abort(newpcb);
        return ERR_ABRT;
    }

    printf("📡 HTTP client connected: %s (slot %d)\n", 
           ip4addr_ntoa(&newpcb->remote_ip), slot);
    
    tcp_arg(newpcb, newpcb);
    tcp_recv(newpcb, http_recv);
    tcp_err(newpcb, http_err_callback);
    tcp_setprio(newpcb, TCP_PRIO_MIN);
    
    return ERR_OK;
}

void http_server_init() {
    printf("\n--- Starting HTTP Server ---\n");
    
    memset(http_connections, 0, sizeof(http_connections));
    mutex_init(&buffer_mutex);
    
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
    printf("║   SE33 Flash Diagnostic System v3.0 (Upload)          ║\n");
    printf("║       MQTT + Web GUI + SD Card + File Upload          ║\n");
    printf("║         Singapore Institute of Technology              ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n");
    printf("\n");

    sd_ready = init_sd_card();
    if (!sd_ready) {
        printf("\n⚠️  WARNING: Running without SD card\n");
        printf("   System will continue but won't have real data\n");
    }

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
        printf("  Check: SSID correct? Password correct? 2.4GHz network?\n");
        return 1;
    }

    printf("✓ WiFi connected successfully!\n");
    const char *ip = ip4addr_ntoa(netif_ip_addr4(netif_default));
    strncpy(pico_ip_address, ip, sizeof(pico_ip_address) - 1);
    printf("✓ IP Address: %s\n", pico_ip_address);

    mqtt_init();
    http_server_init();

    printf("\n========== SYSTEM READY ==========\n");
    printf("✅ SD Card: %s\n", sd_ready ? "Ready" : "Not Available");
    printf("✅ WiFi: Connected (%s)\n", pico_ip_address);
    printf("✅ MQTT: Initializing...\n");
    printf("✅ Web Server: http://%s\n", pico_ip_address);
    printf("📤 File upload enabled (max 100KB)\n");
    printf("\n🚀 System operational - Publishing every 30 seconds\n");
    printf("📊 Open web browser to view and upload files\n");
    printf("==================================\n\n");

    uint32_t last_publish = 0;
    uint32_t last_status = 0;
    uint32_t last_cleanup = 0;

    while (true) {
        cyw43_arch_poll();
        sleep_ms(10);

        uint32_t now = to_ms_since_boot(get_absolute_time());

        if (now - last_cleanup >= 5000) {
            cleanup_old_connections();
            last_cleanup = now;
        }

        if (mqtt_connected && sd_ready && (now - last_publish >= 30000)) {
            printf("\n========== PUBLISHING REPORT ==========\n");
            const char *report = read_json_from_sd();
            mqtt_publish_report(report);
            last_publish = now;

            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
            sleep_ms(200);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
            sleep_ms(100);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
            sleep_ms(200);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        }

        if (now - last_status >= 60000) {
            printf("\n--- System Status ---\n");
            printf("Uptime: %lu seconds\n", now / 1000);
            printf("MQTT: %s\n", mqtt_connected ? "Connected" : "Disconnected");
            printf("SD Card: %s\n", sd_ready ? "Ready" : "Not Ready");
            printf("IP: %s\n", pico_ip_address);
            printf("Current File: %s\n", current_json_file);
            
            int active_connections = 0;
            for (int i = 0; i < MAX_HTTP_CONNECTIONS; i++) {
                if (http_connections[i].in_use) active_connections++;
            }
            printf("Active HTTP connections: %d/%d\n", active_connections, MAX_HTTP_CONNECTIONS);
            
            last_status = now;
        }
    }

    return 0;
}
