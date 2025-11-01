#include "functions.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"
#include <pico/stdio.h>
#include <pico/time.h>
#include <stdint.h>
#include <stdio.h>

// --- Helper Functions for main.c ---

// Helper to print a buffer
void print_report_buffer(const uint8_t *buf, size_t len) {
  printf("--- Report Data (size: %zu) ---\n", len);
  for (size_t i = 0; i < len; ++i) {
    // Print 8 bytes per line
    printf("0x%02X ", buf[i]);
    if ((i + 1) % 8 == 0) {
      printf("\n");
    }
  }
  printf("\n---------------------------------\n");
}

// Helper to get user input from serial
char get_menu_choice() {
  printf("Enter your choice: ");
  char c = getchar_timeout_us(10000000); // 10 second timeout
  printf("%c\n", c);
  return c;
}

// --- Main Application ---

int main() {
  // --- Setup ---
  stdio_init_all();
  sleep_ms(3000); // Wait for serial monitor to connect

  // Initialize the SPI peripheral *only once* at the start.
  spi_master_init();
  printf("SPI Master Initialized.\n");

  // --- Infinite Menu Loop ---
  for (;;) {
    // Print the main menu
    printf("\n\n");
    printf("*****************************************\n");
    printf("* SPI Flash Tool v1.0 (Pico)       *\n");
    printf("*****************************************\n");
    printf("Select Your mode:\n");
    printf("  [1] Full Safe Read Report\n");
    printf("  [2] Individual Safe Commands\n");
    printf("  [3] (Unsafe Commands - Not Implemented)\n");
    printf("*****************************************\n");

    char choice = get_menu_choice();

    switch (choice) {
    // --- [1] Full Safe Read Report ---
    case '1': {
      printf("\n--- Mode 1: Full Safe Read Report ---\n");

      // 1. Calculate the required report size
      size_t report_size = get_expected_report_size();
      printf("Calculated report size: %zu bytes\n", report_size);

      // 2. Create the master buffer
      uint8_t master_rx_buffer[report_size];

      // 3. Call your "SAFEBLOCK" function
      int bytes_stored =
          spi_OPSAFE_transfer(SPI_PORT, master_rx_buffer, report_size);

      // 4. Print the clean, final report
      if (bytes_stored > 0) {
        print_report_buffer(master_rx_buffer, bytes_stored);
      } else {
        printf("--- Safe Block FAILED ---\n");
      }
      break;
    }

    // --- [2] Individual Safe Commands ---
    case '2': {
      printf("\n--- Mode 2: Individual Safe Commands ---\n");

      // 1. Build your dynamic menu
      size_t count = get_safe_command_count();
      printf("Please select a command:\n");
      for (size_t i = 0; i < count; i++) {
        const opcode *cmd = get_command_by_index(i);
        if (cmd != NULL) {
          printf("  [%zu]: %s (Opcode: 0x%02X)\n", i, cmd->description,
                 cmd->opcode);
        }
      }

      // 2. User selects an index
      char idx_choice = get_menu_choice();
      size_t user_choice = idx_choice - '0'; // Convert char '0' to int 0

      // 3. Get the *full struct* for that command
      const opcode *chosen_command = get_command_by_index(user_choice);

      if (chosen_command == NULL) {
        printf("Error: Invalid selection.\n");
        break;
      }

      printf("Running: %s\n", chosen_command->description);

      // 4. Get the size from the struct
      size_t required_len = chosen_command->tx_len;

      // 5. Create buffers of the correct size
      uint8_t my_tx_buffer[required_len];
      uint8_t my_rx_buffer[required_len];

      // 6. Call your new function
      spi_ONE_transfer(SPI_PORT, *chosen_command, my_tx_buffer, my_rx_buffer);

      // 7. Parse the results
      printf("Raw response in my_rx_buffer:\n");
      for (size_t i = 0; i < required_len; i++) {
        printf("  [%zu]: 0x%02X\n", i, my_rx_buffer[i]);
      }
      break;
    }

    // --- [3] Unsafe Commands ---
    case '3':
      printf("\n--- Mode 3: Not Implemented ---\n");
      break;

    default:
      printf("\nInvalid choice. Please try again.\n");
      break;
    }

    sleep_ms(1000); // Wait before re-drawing menu
  }

  return 0; // Unreachable
}
