#include "functions.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"
#include <pico/stdio.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// SAFE OPCODE MAPPING
const opcode safeOps[] = {

    // Read JEDEC ID
    {
        .opcode = 0x9F,
        .tx_len = 4,
        .rx_data_len = 3,
        .description = "Read ManufacturerID, Memory Type and Capacity",
    },
    // Read JEDEC ID ALT-1
    {
        .opcode = 0x9E,
        .tx_len = 4,
        .rx_data_len = 3,
        .description =
            "Alternate Read ManufacturerID, Memory Type and Capacity",
    },
    // Legacy Read Manufacturer/DeviceID
    {
        .opcode = 0x90,
        .tx_len = 6,
        .rx_data_len = 2,
        .description = "Legacy Read Manu/DevID",
    },
    // Read Unique ID
    {
        .opcode = 0x4B,
        .tx_len = 13,
        .rx_data_len = 8,
        .description = "Reads Unique 64-bit Device ID",
    },
    // Read Register-1
    {
        .opcode = 0x05,
        .tx_len = 2,
        .rx_data_len = 1,
        .description = "Read Status Register - 1",
    },
    // Read Register-2
    {
        .opcode = 0x35,
        .tx_len = 2,
        .rx_data_len = 1,
        .description = "Read Status Register - 2",
    },
    // Read Register-3
    {
        .opcode = 0x15,
        .tx_len = 2,
        .rx_data_len = 1,
        .description = "Read Status Register - 3",
    }};

// Calculate number of commands (PRIVATE)
static const size_t num_safe_commands = sizeof(safeOps) / sizeof(safeOps[0]);

// Initialize Master SPI Communications
void spi_master_init(void) {
  // Initialize Serial output/standard C I/O
  stdio_init_all();
  sleep_ms(2000);

  printf("--- SPI MASTER INITIALIZING ---\n");

  // Initialize SPI
  spi_init(SPI_PORT, 100000);
  spi_set_slave(SPI_PORT, false);

  // Set communication Format
  spi_set_format(SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

  // Initialize GPIO as SPI
  gpio_set_function(SCK_PIN, GPIO_FUNC_SPI);
  gpio_set_function(MOSI_PIN, GPIO_FUNC_SPI);
  gpio_set_function(MISO_PIN, GPIO_FUNC_SPI);

  // Configure CS pin for manual toggling
  gpio_init(CS_PIN);
  gpio_set_dir(CS_PIN, GPIO_OUT);
  gpio_put(CS_PIN, 1);

  printf("--- SPI MASTER CONFIGURATION COMPLETE ---\n");
}

// Calculates total expected useful payload size from SafeOPS
size_t get_expected_report_size(void) {
  size_t total_size = 0;
  const size_t cmd_amt = sizeof(safeOps) / sizeof(safeOps[0]);
  for (size_t i = 0; i < cmd_amt; i++) {
    total_size += safeOps[i].rx_data_len;
  }
  return total_size;
}

// Helper function for transmission writes to RX BUFF
int spi_transfer_block(spi_inst_t *spi, const uint8_t *tx_buffer,
                       uint8_t *rx_buffer, size_t len) {
  gpio_put(CS_PIN, 0); // Comms up
  int bytes_transferred = spi_write_read_blocking(spi, tx_buffer, rx_buffer,
                                                  len); // Full duplex transmit
  gpio_put(CS_PIN, 1);                                  // Comms down
  sleep_us(5);                                          // Recovery time
  return bytes_transferred;
}

// Send one single opcode and write to RX_BUFF
int spi_ONE_transfer(spi_inst_t *spi, opcode Opcode, uint8_t *tx_buffer,
                     uint8_t *rx_buffer) {
  if (Opcode.tx_len == 0) {
    return 0;
  }

  // Prepare transmission block
  tx_buffer[0] = Opcode.opcode;
  // Fill rest of array with dummy bytes
  memset(&tx_buffer[1], 0x00, Opcode.tx_len - 1);

  // Transmit
  int bytes_transferred =
      spi_transfer_block(spi, tx_buffer, rx_buffer, Opcode.tx_len);

  return bytes_transferred;
}

// Transfer full SAFE Array block and write responses to the RX buffer
int spi_OPSAFE_transfer(spi_inst_t *spi, uint8_t *master_rx_buffer,
                        size_t max_report_len) {

  // --- Fill master rx with 0x00s first  ---
  memset(master_rx_buffer, 0x00, max_report_len);

  // --- Loop iteration to send every opcode ---
  uint32_t current_rx_offset = 0; // Your RX_OFFSET, renamed

  // Loop over the number of commands for size
  const size_t num_commands = sizeof(safeOps) / sizeof(safeOps[0]);

  // Check if the provided buffer is large enough
  size_t expected_size = get_expected_report_size();
  if (max_report_len < expected_size) {
    printf("ERROR: Master buffer is too small (%zu bytes). Need %zu bytes.\n",
           max_report_len, expected_size);
    return -1; // Error code
  }

  printf("Executing %zu safe commands...\n", num_commands);

  for (size_t i = 0; i < num_commands; i++) {
    const opcode *command = &safeOps[i];

    // Calculate junk length (header size) to find where data starts
    const size_t junk_len = command->tx_len - command->rx_data_len;

    // Create temporary local buffers for this *single* transfer
    uint8_t local_tx_buffer[command->tx_len];
    uint8_t local_rx_buffer[command->tx_len];

    // --- Assemble and Send one command ---

    // Assemble the TX buffer (Opcode + Dummies)
    local_tx_buffer[0] = command->opcode;
    memset(&local_tx_buffer[1], 0x00, command->tx_len - 1); // Fill dummies

    // Call the low-level helper to do the transfer
    spi_transfer_block(spi, local_tx_buffer, local_rx_buffer, command->tx_len);

    // Offset the master rx ---

    // Copy only the useful data from the local buffer
    memcpy(&master_rx_buffer[current_rx_offset], // Destination
           &local_rx_buffer[junk_len],           // Source (starts after junk)
           command->rx_data_len);                // Length (useful data)

    // Advance the offset for the next command's data
    current_rx_offset += command->rx_data_len;
  }

  printf("All commands complete. Total useful data stored: %u bytes.\n",
         (unsigned int)current_rx_offset);

  // Return the number of bytes written to the report
  return current_rx_offset;
}

// Gets the total number of commands in the safeOps Mapping
size_t get_safe_command_count(void) { return num_safe_commands; }

// Gets a pointer to a command struct from the map by the index
const opcode *get_command_by_index(size_t index) {
  if (index >= num_safe_commands) {
    return NULL;
  }
  return &safeOps[index]; // Return a pointer to the item
}

// Format Output to JSON
