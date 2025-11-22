#include "flash_info.h"
#include "functions.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "json.h"
#include "pico/stdlib.h"
#include <pico/stdio.h>
#include <pico/time.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// --- Helper Functions for main.c ---

// Clear the terminal screen
void clear_screen() {
  // ANSI escape code to clear screen and move cursor to home
  printf("\033[2J\033[H");
}

// Print a decorative header
void print_header(const char *title) {
  printf("\n");
  printf("╔════════════════════════════════════════╗\n");
  printf("║ %-38s ║\n", title); // Centered title
  printf("╚════════════════════════════════════════╝\n");
}

// Print a separator line
void print_separator() {
  printf("──────────────────────────────────────────\n");
}

// Print a section header
void print_section(const char *section_name) {
  printf("\n┌─ %s\n", section_name);
}
// Dynamic report function
void print_report_buffer_formatted(const uint8_t *buf, size_t len) {
  clear_screen();
  print_header("FULL CHIP REPORT");

  size_t offset = 0;
  size_t num_commands = get_safe_command_count();

  // Loop through the command map and print a section for each
  for (size_t i = 0; i < num_commands; i++) {
    const opcode *cmd = get_command_by_index(i);
    if (cmd == NULL)
      continue;

    // Print the section header from the command's description
    char section_title[60];
    snprintf(section_title, sizeof(section_title), "%.60s [Opcode: 0x%02X]",
             cmd->description, cmd->opcode);
    print_section(section_title);

    // If this is the JEDEC ID, decode it specially
    if (cmd->opcode == 0x9F) {
      if (offset + cmd->rx_data_len <= len) {
        uint8_t mfr_id = buf[offset];
        uint8_t mem_type = buf[offset + 1];
        uint8_t capacity = buf[offset + 2];
        // Fill flash_info
        int valid = decode_jedec_id(mfr_id, mem_type, capacity);

        // print JEDEC Section
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
      // For all other commands, just print the raw hex data
      printf("│ Data: ");
      for (size_t j = 0; j < cmd->rx_data_len; j++) {
        if (offset + j < len) {
          printf("0x%02X ", buf[offset + j]);
        }
      }
      printf("\n");
    }

    // Pause every 3 sections to avoid terminal overflow
    if (i > 0 && (i % 3) == 2) {
      printf("\nPress any key to continue...");
      get_menu_choice();
      clear_screen();

      // Page numbering support
      size_t sections_per_page = 3;
      size_t current_page = (i / sections_per_page) + 1;
      size_t total_pages =
          (num_commands + sections_per_page - 1) / sections_per_page;

      char page_title[50];
      snprintf(page_title, sizeof(page_title),
               "FULL CHIP REPORT (Page %zu / %zu)", current_page, total_pages);

      print_header(page_title);
    }

    // Advance the offset by this command's payload size
    offset += cmd->rx_data_len;
  }

  print_separator();
  printf("Total useful bytes read: %zu bytes\n", offset);
  print_separator();
}

// Helper to print individual command results (cleaned up)
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

  // Assessment
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

// Helper to get user input from serial (blocking)
char get_menu_choice() {
  char c;
  do {
    c = getchar();
  } while (c == PICO_ERROR_TIMEOUT || c == '\n' || c == '\r');

  printf("%c\n", c); // Echo the choice
  return c;
}

// Print the main menu (cleaned up)
void print_main_menu() {
  clear_screen();
  printf("\n");
  printf("╔════════════════════════════════════════╗\n");
  printf("║                                        ║\n");
  printf("║           SPI Flash Identifier         ║\n");
  printf("║                 v1.0                   ║\n");
  printf("║                                        ║\n");
  printf("╚════════════════════════════════════════╝\n");
  printf("\n");
  printf("  [1] Identify Chip (Quick JEDEC ID)\n");
  printf("  [2] Full Safe Read Report\n");
  printf("  [3] Individual Safe Commands\n");
  printf("  [4] Export Full Safe Report in JSON\n");
  printf("  [5] Execute Destructive Commands (Write/Erase)\n");
  printf("\n");
  printf("──────────────────────────────────────────\n");
}

// --- Main Application ---

int main() {
  // --- Setup ---
  stdio_init_all();
  sleep_ms(3000); // Wait for serial monitor to connect

  // Initialize the SPI peripheral *only once* at the start.
  spi_master_init();
  printf("\nSPI Master Ready.\n");

  // --- Infinite Loop ---
  for (;;) {
    print_main_menu();
    printf("Enter your choice: ");
    char choice = get_menu_choice();

    switch (choice) {

      // --- [1] Identify Chip ---

    case '1': {
      clear_screen();
      print_header("CHIP IDENTIFICATION");
      printf("\nReading JEDEC ID (0x9F)...\n");
      print_separator();

      const opcode *jedec_cmd = get_command_by_index(0);

      uint8_t tx_buffer[1];
      uint8_t rx_buffer[3];

      int result = spi_ONE_transfer(SPI_PORT, *jedec_cmd, tx_buffer, rx_buffer);

      gpio_put(CS_PIN, 1);

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

    // --- [2] Full Safe Read Report (Formatted) ---
    case '2': {
      clear_screen();
      print_header("EXECUTING SAFE READ SEQUENCE");

      size_t report_size = get_expected_report_size();
      printf("\nReport size: %zu bytes\n", report_size);
      printf("Executing %zu safe commands...\n", get_safe_command_count());
      print_separator();

      // Check if report size is 0
      if (report_size == 0) {
        printf("Error: No commands defined in map.\n");
        printf("\nPress any key to return to menu...");
        get_menu_choice();
        break;
      }

      uint8_t master_rx_buffer[report_size];

      int bytes_stored =
          spi_OPSAFE_transfer(SPI_PORT, master_rx_buffer, report_size);

      if (bytes_stored > 0) {
        printf("\nRead complete! Analyzing data...\n");
        sleep_ms(500); // Brief pause for effect
        // Call the new DYNAMIC report printer
        print_report_buffer_formatted(master_rx_buffer, bytes_stored);
      } else {
        printf("\nSafe Block Transfer FAILED\n");
        print_separator();
      }

      printf("\nPress any key to return to menu...");
      get_menu_choice();
      break;
    }

    // --- [3] Individual Safe Commands ---
    case '3': {
      clear_screen();
      print_header("INDIVIDUAL COMMAND SELECTION");

      size_t count = get_safe_command_count();
      printf("\n%zu commands available:\n\n", count);

      for (size_t i = 0; i < count; i++) {
        const opcode *cmd = get_command_by_index(i);
        if (cmd != NULL) {
          printf("  [%zu] %s\n       Opcode: 0x%02X | Total-TX: %zu | Data-RX: "
                 "%zu\n\n",
                 i, cmd->description, cmd->opcode, cmd->tx_len,
                 cmd->rx_data_len);
        }
      }

      print_separator();
      printf("Enter choice [0-%zu]: ", count - 1);
      char idx_choice = get_menu_choice();
      size_t user_choice = idx_choice - '0'; // Convert char '0' to int 0

      const opcode *chosen_command = get_command_by_index(user_choice);

      if (chosen_command == NULL) {
        printf("\nInvalid selection.\n");
        printf("\nPress any key to return to menu...");
        get_menu_choice();
        break;
      }

      printf("\nExecuting command...\n");
      sleep_ms(300);

      size_t required_len = chosen_command->tx_len;
      uint8_t my_tx_buffer[required_len];
      uint8_t my_rx_buffer[required_len];

      spi_ONE_transfer(SPI_PORT, *chosen_command, my_tx_buffer, my_rx_buffer);

      // Calculate where data starts
      size_t data_start = chosen_command->tx_len - chosen_command->rx_data_len;

      print_individual_command(
          chosen_command->description, chosen_command->opcode, my_rx_buffer,
          required_len, data_start, chosen_command->rx_data_len);

      printf("\nPress any key to return to menu...");
      get_menu_choice();
      break;
    }
      // --- [4] JSON EXPORT ---

    case '4': {
      clear_screen();
      print_header("JSON EXPORT");

      // Compute total data we expect from safeOps
      size_t expected = get_expected_report_size();
      if (expected == 0) {
        printf("\nERROR: No commands defined!\n");
        print_separator();
        printf("\nPress any key...");
        get_menu_choice();
        break;
      }

      // Allocate RX buffer for safeOps data
      uint8_t *report = malloc(expected);
      if (!report) {
        printf("\nMemory allocation failed for report buffer!\n");
        print_separator();
        printf("\nPress any key...");
        get_menu_choice();
        break;
      }

      // Run the full safe-op scan
      int stored = spi_OPSAFE_transfer(SPI_PORT, report, expected);
      if (stored <= 0) {
        printf("\nSafe block transfer failed.\n");
        free(report);
        print_separator();
        printf("\nPress any key...");
        get_menu_choice();
        break;
      }

      // JSON buffer sizing
      size_t json_cap = (size_t)stored * 12 + 4096; // safe margin
      char *json = malloc(json_cap);
      if (!json) {
        printf("\nJSON allocation failed!\n");
        free(report);
        print_separator();
        printf("\nPress any key...");
        get_menu_choice();
        break;
      }

      // Produce JSON: includes raw hex + translated manufacturer
      size_t written = json_export_full_report(json, json_cap, report, stored);
      if (written == 0) {
        printf("\nJSON export failed (buffer too small?)\n");
      } else {
        printf("\n%s\n", json);
      }

      free(json);
      free(report);

      print_separator();
      printf("\nPress any key to return...");
      get_menu_choice();
      break;
    }

    // --- [5] Execute Destructive Commands (Write/Erase) ---
    case '5': {
        clear_screen();
        print_header("DESTRUCTIVE COMMANDS MENU");

        printf("Select Operation:\n");
        printf("  [1] Page Program (Write up to 256 bytes)\n");
        printf("  [2] Sector Erase 4KB\n");
        printf("\nEnter choice: ");
        char op_choice = get_menu_choice();

        if (op_choice != '1' && op_choice != '2') {
            printf("\nInvalid selection.\nPress any key to return...");
            get_menu_choice();
            break;
        }

        // --- Get 24-bit address from user ---
        uint32_t address = 0;
        printf("\nEnter 24-bit address in hex (e.g., 0x000000): 0x");
        scanf("%06x", &address);

        uint8_t tx[260] = {0};  // Max page size + opcode + 3-byte addr
        uint8_t rx[1] = {0};

        tx[1] = (address >> 16) & 0xFF;
        tx[2] = (address >> 8) & 0xFF;
        tx[3] = address & 0xFF;

        // --- Send Write Enable first ---
        printf("\nSending Write Enable (WREN)...\n");
        spi_ONE_transfer(SPI_PORT, desOps[0], tx, rx);
        sleep_ms(1);

        if (op_choice == '1') {
            // Page Program
            tx[0] = desOps[2].opcode; // 0x02

            // Ask user for number of bytes to write
            size_t data_len = 0;
            printf("Enter number of bytes to write (1-256): ");
            scanf("%zu", &data_len);
            if (data_len > 256) data_len = 256;

            // For demo, fill payload with 0xAA
            for (size_t i = 0; i < data_len; i++) {
                tx[4 + i] = 0xAA;
            }

            printf("\nWriting %zu bytes to address 0x%06X...\n", data_len, address);
            spi_ONE_transfer(SPI_PORT, desOps[2], tx, rx);

        } else if (op_choice == '2') {
            // Sector Erase 4KB
            tx[0] = desOps[3].opcode; // 0x20
            printf("\nErasing 4KB sector at address 0x%06X...\n", address);
            spi_ONE_transfer(SPI_PORT, desOps[3], tx, rx);
        }

        // --- Send Write Disable after operation ---
        printf("Disabling write (WRDI)...\n");
        spi_ONE_transfer(SPI_PORT, desOps[1], tx, rx);

        // --- Read back 16 bytes to verify ---
        opcode readDataOp = {
            .opcode = 0x03,
            .tx_len = 4,
            .rx_data_len = 16,
            .description = "Read Data"
        };
 
        tx[0] = readDataOp.opcode;
        tx[1] = (address >> 16) & 0xFF;
        tx[2] = (address >> 8) & 0xFF;
        tx[3] = address & 0xFF;

        uint8_t read_buffer[16] = {0};
        spi_ONE_transfer(SPI_PORT, readDataOp, tx, read_buffer);

        printf("\nRead back 16 bytes at 0x%06X:\n", address);
        for (int i = 0; i < 16; i++) {
            printf("0x%02X ", read_buffer[i]);
        }
        printf("\n");

        // --- Done ---
        print_separator();
        printf("Operation complete! Press any key to return to menu...");
        get_menu_choice();
        break;
    }

    default:
      if (choice != '\n' && choice != '\r' && choice != PICO_ERROR_TIMEOUT) {
        printf("\nInvalid choice. Please try again.\n");
        sleep_ms(1000);
      }
      break;
    }
  }

  return 0; // Unreachable
}
