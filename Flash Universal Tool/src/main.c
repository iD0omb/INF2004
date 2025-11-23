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
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h> 
#include <string.h>
#include "web_server.h"
#include "web_server.h"
// ========== Flash Commands (Readable) ==========
#define FLASH_WRITE_ENABLE 0x06
#define FLASH_READ_STATUS 0x05
#define FLASH_READ_DATA 0x03
#define FLASH_PAGE_PROGRAM 0x02
#define FLASH_SECTOR_ERASE 0x20
#define FLASH_PAGE_SIZE 256
#define FLASH_SECTOR_SIZE 4096

// ========== Global Variables ==========

char json_buffer[JSON_BUFFER_SIZE];
struct tcp_pcb *http_server_pcb;
char pico_ip_address[16] = "0.0.0.0";
bool sd_ready = false;

// SPI state
bool spi_initialized = false;
uint8_t last_jedec_id[3] = {0xFF, 0xFF, 0xFF};

mutex_t spi_mutex;
mutex_t buffer_mutex;

// ========== Internal Flash Helpers ==========

// Poll status register until busy bit is cleared
static bool flash_wait_ready(uint32_t timeout_ms) {
  uint8_t status;
  uint8_t cmd = FLASH_READ_STATUS;
  absolute_time_t deadline = make_timeout_time_ms(timeout_ms);

  do {
    gpio_put(CS_PIN, 0); // CS Down
    spi_write_blocking(SPI_PORT, &cmd, 1);
    spi_read_blocking(SPI_PORT, 0xFF, &status, 1);
    gpio_put(CS_PIN, 1); // CS Up

    if (!(status & 0x01)) { // Check BUSY bit (Bit 0)
      return true;
    }
    sleep_us(100);
  } while (!time_reached(deadline));

  return false;
}

// Send Write Enable Latch command
static void flash_set_write_enable(void) {
  uint8_t cmd = FLASH_WRITE_ENABLE;
  gpio_put(CS_PIN, 0); // CS Down
  spi_write_blocking(SPI_PORT, &cmd, 1);
  gpio_put(CS_PIN, 1); // CS Up
}

// ========== Flash Operations==========

bool flash_read_bytes(uint32_t address, uint8_t *buffer, size_t size) {
  if (!spi_initialized)
    return false;

  mutex_enter_blocking(&spi_mutex);

  uint8_t cmd_seq[4];
  cmd_seq[0] = FLASH_READ_DATA;
  cmd_seq[1] = (address >> 16) & 0xFF;
  cmd_seq[2] = (address >> 8) & 0xFF;
  cmd_seq[3] = address & 0xFF;

  gpio_put(CS_PIN, 0); // CS Down
  spi_write_blocking(SPI_PORT, cmd_seq, 4);
  spi_read_blocking(SPI_PORT, 0xFF, buffer, size);
  gpio_put(CS_PIN, 1); // CS Up

  mutex_exit(&spi_mutex);
  return true;
}

bool flash_erase_sector(uint32_t address) {
  if (!spi_initialized)
    return false;

  // Safety: Align to sector start
  address = address & ~(FLASH_SECTOR_SIZE - 1);

  mutex_enter_blocking(&spi_mutex);

  flash_set_write_enable();

  uint8_t cmd_seq[4];
  cmd_seq[0] = FLASH_SECTOR_ERASE;
  cmd_seq[1] = (address >> 16) & 0xFF;
  cmd_seq[2] = (address >> 8) & 0xFF;
  cmd_seq[3] = address & 0xFF;

  gpio_put(CS_PIN, 0); // CS Down
  spi_write_blocking(SPI_PORT, cmd_seq, 4);
  gpio_put(CS_PIN, 1); // CS Up

  // Sector erase can take 50ms to 400ms depending on chip
  bool result = flash_wait_ready(500);

  mutex_exit(&spi_mutex);
  return result;
}

bool flash_program_data(uint32_t addr, const uint8_t *data, size_t len) {
  if (!spi_initialized)
    return false;

  mutex_enter_blocking(&spi_mutex);

  uint32_t current_addr = addr;
  const uint8_t *current_ptr = data;
  size_t remaining_bytes = len;
  uint8_t cmd_seq[4];

  while (remaining_bytes > 0) {
    // Calculate remaining space in current page
    uint16_t page_offset = current_addr % FLASH_PAGE_SIZE;
    size_t space_in_page = FLASH_PAGE_SIZE - page_offset;
    size_t chunk_len =
        (remaining_bytes < space_in_page) ? remaining_bytes : space_in_page;

    flash_set_write_enable();

    cmd_seq[0] = FLASH_PAGE_PROGRAM;
    cmd_seq[1] = (current_addr >> 16) & 0xFF;
    cmd_seq[2] = (current_addr >> 8) & 0xFF;
    cmd_seq[3] = current_addr & 0xFF;

    gpio_put(CS_PIN, 0); // CS Down
    spi_write_blocking(SPI_PORT, cmd_seq, 4);
    spi_write_blocking(SPI_PORT, current_ptr, chunk_len);
    gpio_put(CS_PIN, 1); // CS Up

    if (!flash_wait_ready(50)) { // Page program usually < 3ms
      mutex_exit(&spi_mutex);
      printf("✗ Flash Write Timeout at 0x%06X\n", (unsigned int)current_addr);
      return false;
    }

    current_addr += chunk_len;
    current_ptr += chunk_len;
    remaining_bytes -= chunk_len;
  }

  mutex_exit(&spi_mutex);
  return true;
}

// ========== SPI Diagnostic Functions ==========

// Run full SPI diagnostic and generate JSON report
bool run_spi_diagnostic(char *json_out, size_t json_cap) {
  if (!spi_initialized) {
    snprintf(json_out, json_cap, "{\"error\":\"SPI not initialized\"}");
    return false;
  }

  mutex_enter_blocking(&spi_mutex);

  // Get expected report size
  size_t expected = get_expected_report_size();
  if (expected == 0) {
    mutex_exit(&spi_mutex);
    snprintf(json_out, json_cap, "{\"error\":\"No commands defined\"}");
    return false;
  }

  // Allocate buffer for raw SPI data
  uint8_t *report = malloc(expected);
  if (!report) {
    mutex_exit(&spi_mutex);
    snprintf(json_out, json_cap, "{\"error\":\"Memory allocation failed\"}");
    return false;
  }

  // Execute safe operation transfer
  int stored = spi_OPSAFE_transfer(SPI_PORT, report, expected);

  if (stored <= 0) {
    free(report);
    mutex_exit(&spi_mutex);
    snprintf(json_out, json_cap, "{\"error\":\"SPI transfer failed\"}");
    return false;
  }

  // Store JEDEC ID for quick reference
  if (stored >= 3) {
    last_jedec_id[0] = report[0];
    last_jedec_id[1] = report[1];
    last_jedec_id[2] = report[2];
  }

  // Generate JSON from raw data
  size_t written = json_export_full_report(json_out, json_cap, report, stored);

  free(report);
  mutex_exit(&spi_mutex);

  return (written > 0);
}

// Quick JEDEC ID read
bool read_jedec_id(uint8_t *mfr, uint8_t *mem_type, uint8_t *capacity) {
  if (!spi_initialized) {
    return false;
  }

  mutex_enter_blocking(&spi_mutex);

  const opcode *jedec_cmd = get_command_by_index(0); // JEDEC is first command
  if (!jedec_cmd) {
    mutex_exit(&spi_mutex);
    return false;
  }

  uint8_t tx_buffer[1];
  uint8_t rx_buffer[3];

  int result = spi_ONE_transfer(SPI_PORT, *jedec_cmd, tx_buffer, rx_buffer);
  gpio_put(CS_PIN, 1);

  mutex_exit(&spi_mutex);

  if (result == 3) {
    *mfr = rx_buffer[0];
    *mem_type = rx_buffer[1];
    *capacity = rx_buffer[2];

    last_jedec_id[0] = rx_buffer[0];
    last_jedec_id[1] = rx_buffer[1];
    last_jedec_id[2] = rx_buffer[2];

    return true;
  }

  return false;
}

// ========== CLI LOOP ==================

void clear_screen(void) { printf("\033[2J\033[H"); }

void print_header(const char *title) {
  printf("\n");
  printf("╔════════════════════════════════════════╗\n");
  printf("║ %-38s ║\n", title);
  printf("╚════════════════════════════════════════╝\n");
}

void print_separator(void) {
  printf("──────────────────────────────────────────\n");
}

void print_section(const char *section_name) {
  printf("\n┌─ %s\n", section_name);
}

// --- CLI Helper functions ---
void get_input_line(char *buffer, int max_len) {
  int i = 0;
  char c;
  while (i < max_len - 1) {
    c = getchar();
    if (c == '\r' || c == '\n') {
      printf("\n");
      break;
    }
    if (c == '\b' || c == 0x7F) { // Backspace
      if (i > 0) {
        printf("\b \b");
        i--;
      }
    } else {
      printf("%c", c);
      buffer[i++] = c;
    }
  }
  buffer[i] = '\0';
}

uint32_t get_hex_input(const char *prompt) {
  char buf[32];
  printf("%s", prompt);
  get_input_line(buf, 32);
  return (uint32_t)strtoul(buf, NULL, 0); // 0 base handles 0x prefix or decimal
}

bool confirm_destructive(const char *action) {
  printf("\n⚠️  WARNING: %s\n", action);
  printf("This operation is DESTRUCTIVE. Continue? (y/n): ");
  char c;
  do {
    c = getchar();
  } while (c == '\n' || c == '\r');
  printf("%c\n", c);
  return (c == 'y' || c == 'Y');
}

void print_report_buffer_formatted(const uint8_t *buf, size_t len) {
  clear_screen();
  print_header("FULL CHIP REPORT");

  size_t offset = 0;
  size_t num_commands = get_safe_command_count();

  for (size_t i = 0; i < num_commands; i++) {
    const opcode *cmd = get_command_by_index(i);
    if (cmd == NULL)
      continue;

    char section_title[60];
    snprintf(section_title, sizeof(section_title), "%.60s [Opcode: 0x%02X]",
             cmd->description, cmd->opcode);
    print_section(section_title);

    if (cmd->opcode == 0x9F) {
      if (offset + cmd->rx_data_len <= len) {
        uint8_t mfr_id = buf[offset];
        uint8_t mem_type = buf[offset + 1];
        uint8_t capacity = buf[offset + 2];

        int valid = decode_jedec_id(mfr_id, mem_type, capacity);

        if (valid) {
          print_jedec_report(mfr_id, mem_type, capacity);
        } else {
          print_section("JEDEC ID Analysis");
          printf("| ERROR - Invalid JEDEC ID Response\n");
          print_separator();
        }
      }
    } else if (cmd->opcode == 0x5A && cmd->rx_data_len == 8) {
      decode_sfdp_header(&buf[offset]);
    } else if (cmd->opcode == 0x5A && cmd->rx_data_len == 24) {
      decode_sfdp_param_headers(&buf[offset]);
    } else {
      printf("│ Data: ");
      for (size_t j = 0; j < cmd->rx_data_len; j++) {
        if (offset + j < len) {
          printf("0x%02X ", buf[offset + j]);
        }
      }
      printf("\n");
    }

    if (i > 0 && (i % 3) == 2) {
      printf("\nPress any key to continue...");
      // Using local get_char wrapper not defined, using getchar direct
      char c;
      do {
        c = getchar();
      } while (c == PICO_ERROR_TIMEOUT);
      clear_screen();

      size_t sections_per_page = 3;
      size_t current_page = (i / sections_per_page) + 1;
      size_t total_pages =
          (num_commands + sections_per_page - 1) / sections_per_page;

      char page_title[50];
      snprintf(page_title, sizeof(page_title),
               "FULL CHIP REPORT (Page %zu / %zu)", current_page, total_pages);

      print_header(page_title);
    }

    offset += cmd->rx_data_len;
  }

  print_separator();
  printf("Total useful bytes read: %zu bytes\n", offset);
  print_separator();
}

void print_individual_command(const char *name, uint8_t opcode,
                              const uint8_t *rx_buffer, size_t total_len,
                              size_t data_start, size_t data_len) {
  clear_screen();
  print_header(name);

  printf("\nOpcode: 0x%02X | Total Length: %zu bytes\n", opcode, total_len);
  print_separator();

  printf("\n┌─ Raw Response (Full %zu bytes)\n", total_len);
  for (size_t i = 0; i < total_len; i++) {
    if (i % 8 == 0)
      printf("│ ");
    printf("0x%02X ", rx_buffer[i]);
    if ((i + 1) % 8 == 0)
      printf("\n");
  }
  if (total_len % 8 != 0)
    printf("\n");

  print_section("Data Breakdown");
  printf("│ Junk (Protocol) : ");
  for (size_t i = 0; i < data_start; i++) {
    printf("0x%02X ", rx_buffer[i]);
  }
  printf("\n");

  printf("│ Payload Data    : ");
  bool all_zero = true;
  bool all_ff = true;

  for (size_t i = data_start; i < data_start + data_len; i++) {
    printf("0x%02X ", rx_buffer[i]);
    if (rx_buffer[i] != 0x00)
      all_zero = false;
    if (rx_buffer[i] != 0xFF)
      all_ff = false;
  }
  printf("\n");

  print_section("Assessment");
  printf("│ ");
  if (all_ff) {
    printf("ERROR - All 0xFF (no device response)\n");
  } else if (all_zero && data_len > 1) {
    printf("WARNING - All zeros (stuck low or unprogrammed)\n");
  } else if (opcode == 0x9F && rx_buffer[data_start] != 0xFF) {
    printf("VALID - JEDEC ID response received\n");
  } else {
    printf("VALID - Response received\n");
  }

  print_separator();
}

char get_menu_choice(void) {
  char c;
  do {
    c = getchar();
  } while (c == PICO_ERROR_TIMEOUT || c == '\n' || c == '\r');

  printf("%c\n", c); // Echo the choice
  return c;
}

void print_main_menu(void) {
  clear_screen();
  printf("\n");
  printf("╔════════════════════════════════════════╗\n");
  printf("║                                        ║\n");
  printf("║           SPI Flash Identifier         ║\n");
  printf("║                v2.1                    ║\n");
  printf("║                                        ║\n");
  printf("╚════════════════════════════════════════╝\n");
  printf("\n");
  printf("  [1] Identify Chip (Quick JEDEC ID)\n");
  printf("  [2] Full Safe Read Report\n");
  printf("  [3] Individual Safe Commands\n");
  printf("  [4] Export Full Safe Report in JSON\n");
  printf("  --------------------------------\n");
  printf("  [5] READ Flash (Raw Bytes)\n");
  printf("  [6] WRITE Flash (Text String)\n");
  printf("  [7] ERASE Flash (Sector Aligned)\n");
  printf("──────────────────────────────────────────\n");
  printf("  [8] Opcode Fuzzing (Dangerous)\n");
  printf("──────────────────────────────────────────\n");
}

void cli_core(void) {
  sleep_ms(2000);
  printf("\n[CLI] Starting SPI Flash Tool CLI on Core 1...\n");

  while (true) {
    print_main_menu();
    printf("Enter your choice: ");
    char choice = get_menu_choice();

    switch (choice) {
    case '1': {
      // --- IDENTIFY CHIP ---
      clear_screen();
      print_header("CHIP IDENTIFICATION");
      printf("\nReading JEDEC ID (0x9F)...\n");
      print_separator();

      uint8_t tx_buffer[1];
      uint8_t rx_buffer[3];
      const opcode *jedec_cmd = get_command_by_index(0);

      int result = spi_ONE_transfer(SPI_PORT, *jedec_cmd, tx_buffer, rx_buffer);

      if (result == 3) {
        printf("Raw JEDEC: %02X %02X %02X\n", rx_buffer[0], rx_buffer[1],
               rx_buffer[2]);

        if (decode_jedec_id(rx_buffer[0], rx_buffer[1], rx_buffer[2])) {
          print_jedec_report(rx_buffer[0], rx_buffer[1], rx_buffer[2]);
        } else {
          printf("ERROR: Invalid JEDEC response\n");
        }
      } else {
        printf("ERROR: JEDEC read failed\n");
      }

      print_separator();
      get_menu_choice();
      break;
    }
    case '2': {
      // --- FULL SAFE REPORT ---
      clear_screen();
      print_header("EXECUTING SAFE READ SEQUENCE");

      size_t report_size = get_expected_report_size();
      printf("\nReport size: %zu bytes\n", report_size);
      print_separator();

      uint8_t master_rx_buffer[report_size];
      int stored = spi_OPSAFE_transfer(SPI_PORT, master_rx_buffer, report_size);

      if (stored > 0) {
        print_report_buffer_formatted(master_rx_buffer, stored);
      } else {
        printf("\nSafe Block Transfer FAILED\n");
      }

      printf("\nPress any key to return...");
      get_menu_choice();
      break;
    }

    case '3': {
      clear_screen();
      print_header("INDIVIDUAL SAFE COMMANDS");

      size_t count = get_safe_command_count();
      printf("\n%zu commands available:\n\n", count);

      for (size_t i = 0; i < count; i++) {
        const opcode *cmd = get_command_by_index(i);
        printf("[%zu] %s (0x%02X)  TX:%zu RX:%zu\n", i, cmd->description,
               cmd->opcode, cmd->tx_len, cmd->rx_data_len);
      }

      print_separator();
      printf("Choose index: ");
      char idx = get_menu_choice();
      size_t cmd_index = idx - '0';

      const opcode *cmd = get_command_by_index(cmd_index);
      if (!cmd)
        break;

      size_t tx_len = cmd->tx_len;
      size_t rx_len = cmd->rx_data_len;

      uint8_t txb[tx_len];
      uint8_t rxb[rx_len];

      memset(txb, 0, tx_len);
      memset(rxb, 0, rx_len);

      mutex_enter_blocking(&spi_mutex);
      int res = spi_ONE_transfer(SPI_PORT, *cmd, txb, rxb);
      gpio_put(CS_PIN, 1);
      mutex_exit(&spi_mutex);

      if (res != (int)rx_len) {
        printf("ERROR: SPI returned %d bytes (expected %d)\n", res,
               (int)rx_len);
        break;
      }

      print_individual_command(cmd->description, cmd->opcode, rxb, rx_len, 0,
                               rx_len);

      printf("\nPress any key...");
      get_menu_choice();
      break;
    }

    case '4': {
      // --- JSON EXPORT ---
      clear_screen();
      print_header("JSON EXPORT");

      size_t expected = get_expected_report_size();
      uint8_t *report = malloc(expected);

      int stored2 = spi_OPSAFE_transfer(SPI_PORT, report, expected);
      size_t json_cap = stored2 * 12 + 4096;
      char *json = malloc(json_cap);

      json_export_full_report(json, json_cap, report, stored2);

      printf("%s\n", json);

      free(json);
      free(report);

      print_separator();
      printf("\nPress any key...");
      get_menu_choice();
      break;
    }

    case '5': {
      // --- READ FLASH (Canonical Hex + ASCII) ---
      clear_screen();
      print_header("READ FLASH");
      uint32_t addr = get_hex_input("Enter Start Address (e.g. 0x0000): ");
      uint32_t len = get_hex_input("Enter Length (bytes): ");

      // Safety limit for CLI buffer
      if (len > 4096) {
        printf("Limiting length to 4096 bytes for CLI display.\n");
        len = 4096;
      }

      uint8_t *buf = malloc(len);
      if (!buf) {
        printf("Memory allocation failed.\n");
        break;
      }

      // Perform the Read
      // REMOVED: mutex_enter_blocking(&spi_mutex); <-- Caused the hang

      // The function below already handles the locking internally
      bool success = flash_read_bytes(addr, buf, len);

      if (success) {
        printf("\nReading %u bytes from 0x%06X:\n", (unsigned int)len,
               (unsigned int)addr);
        print_separator();

        // --- CANONICAL HEX DUMP LOOP ---
        for (uint32_t i = 0; i < len; i += 16) {
          // 1. Print Offset (e.g., 0x000010)
          printf("0x%06X: ", (unsigned int)(addr + i));

          // 2. Print Hex Bytes (Left Side)
          for (uint32_t j = 0; j < 16; j++) {
            if (i + j < len) {
              printf("%02X ", buf[i + j]);
            } else {
              printf("   "); // Padding for partial lines
            }
          }

          printf("| ");

          // 3. Print ASCII (Right Side)
          for (uint32_t j = 0; j < 16; j++) {
            if (i + j < len) {
              uint8_t c = buf[i + j];
              // Check if character is printable (ASCII 32-126)
              printf("%c", (c >= 32 && c <= 126) ? c : '.');
            }
          }
          printf("\n");
        }
        print_separator();

      } else {
        printf("\n✗ Read Failed (SPI Error)\n");
      }

      free(buf);
      printf("\nPress any key...");
      get_menu_choice();
      break;
    }
    case '6': {
      // --- WRITE FLASH ---
      clear_screen();
      print_header("WRITE FLASH (Text Mode)");
      uint32_t addr = get_hex_input("Enter Start Address (e.g. 0x0000): ");

      printf("Enter string to write: ");
      char text_buf[256];
      get_input_line(text_buf, 256);
      size_t len = strlen(text_buf);

      if (confirm_destructive(
              "Writing data will overwrite existing content.")) {
        if (flash_program_data(addr, (uint8_t *)text_buf, len)) {
          printf("\n✓ Successfully wrote %zu bytes to 0x%06X\n", len,
                 (unsigned int)addr);
        } else {
          printf("\n✗ Write Failed.\n");
        }
      } else {
        printf("\nOperation cancelled.\n");
      }
      printf("\nPress any key...");
      get_menu_choice();
      break;
    }

    case '7': {
      // --- ERASE SECTOR ---
      clear_screen();
      print_header("ERASE SECTOR");
      uint32_t addr = get_hex_input("Enter Address in Sector (e.g. 0x1000): ");

      // Align display to sector
      uint32_t sector_start = addr & ~(FLASH_SECTOR_SIZE - 1);

      char msg[64];
      snprintf(msg, 64, "Erasing 4KB sector at 0x%06X",
               (unsigned int)sector_start);

      if (confirm_destructive(msg)) {
        printf("Erasing...");
        if (flash_erase_sector(sector_start)) {
          printf("\n✓ Sector Erased Successfully\n");
        } else {
          printf("\n✗ Erase Failed.\n");
        }
      } else {
        printf("\nOperation cancelled.\n");
      }
      printf("\nPress any key...");
      get_menu_choice();
      break;
    }
      // --- NEW OPTION 8 ---
    case '8': {
      clear_screen();
      print_header("OPCODE FUZZER");
      printf("\nWARNING: This scans all 256 opcodes (0x00-0xFF).\n");
      printf("This may trigger undocumented Erase or Lock commands.\n");
      printf("If the chip hangs, power cycle the device.\n");

      if (confirm_destructive("Start Blind Opcode Scan?")) {
        // Lock SPI bus so Web/MQTT doesn't interrupt us
        mutex_enter_blocking(&spi_mutex);

        // Call the function we added to function.c
        spi_fuzz_scan(SPI_PORT);

        mutex_exit(&spi_mutex);
      } else {
        printf("\nScan cancelled.\n");
      }

      printf("\nPress any key...");
      get_menu_choice();
      break;
    }

    default: {
      printf("\nInvalid choice.\n");
      sleep_ms(1000);
    }
    }
  }
}

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
  sd_ready = sd_full_init(); // Calls the new wrapper in sd_card.c
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
  //Start HTTP server
  http_server_init(pico_ip_address);
  // Initialize MQTT - CHANGED: Call the function from mqtt_ops.c
  mqtt_init(); 

  printf("\n========== SYSTEM READY ==========\n");
  printf("✅ SPI: Ready\n");
  printf("✅ WiFi: %s\n", pico_ip_address);
  printf("✅ Web GUI: http://%s\n", pico_ip_address);
  // CHANGED: Use accessor
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
      // CHANGED: Use accessor
      printf("MQTT: %s\n", mqtt_is_connected() ? "Connected" : "Disconnected"); 
      printf("Last JEDEC: %02X %02X %02X\n", last_jedec_id[0], last_jedec_id[1],
             last_jedec_id[2]);
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