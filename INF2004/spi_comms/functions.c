#include "functions.h"
#include "flash_db.h"
#include "flash_info.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"
#include <pico/stdio.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// SAFE OPCODE MAPPING - Generic commands that work across most SPI flash chips
const opcode safeOps[] = {

    // Read JEDEC ID (Standard across all manufacturers)
    {
        .opcode = 0x9F,
        .tx_len = 4,
        .rx_data_len = 3,
        .description = "Read JEDEC ID (Mfr/Type/Capacity)",
    },
    // Read Status Register-1 (Standard)
    {
        .opcode = 0x05,
        .tx_len = 2,
        .rx_data_len = 1,
        .description = "Read Status Register-1",
    },
    // Read Status Register-2 (Common but not universal)
    {
        .opcode = 0x35,
        .tx_len = 2,
        .rx_data_len = 1,
        .description = "Read Status Register-2",
    },
    // Read Status Register-3 (Common but not universal)
    {
        .opcode = 0x15,
        .tx_len = 2,
        .rx_data_len = 1,
        .description = "Read Status Register-3",
    },
    // Legacy Read Manufacturer/DeviceID
    {
        .opcode = 0x90,
        .tx_len = 6,
        .rx_data_len = 2,
        .description = "Legacy Read Mfr/Device ID",
    },
    // Read Electronic Signature (Alternative ID method)
    {
        .opcode = 0xAB,
        .tx_len = 5,
        .rx_data_len = 1,
        .description = "Read Electronic Signature",
    },
    // Read Unique ID (If supported)
    {
        .opcode = 0x4B,
        .tx_len = 13,
        .rx_data_len = 8,
        .description = "Read Unique ID (64-bit)",
    },
    // Read SFDP Headers
    {
        .opcode = 0x5A,
        .tx_len = 13,
        .rx_data_len = 8,
        .description = "Read SFDP Table Headers",
    },
    // Read SFDP Parameter Headers (After SFDP Headers)
    {
        .opcode = 0x5A,
        .tx_len = 29, // address + dummie + 24 data bytes
        .rx_data_len = 24,
        .description = "SFDP Parameter Headers",
    }};

// Calculate number of commands (PRIVATE)
static const size_t num_safe_commands = sizeof(safeOps) / sizeof(safeOps[0]);

// Global instance of flash_info
flash_info_t flash_info = {0};

// Initialize Master SPI Communications
void spi_master_init(void) {
  // Initialize Serial output/standard C I/O

  sleep_ms(2000);

  printf("--- SPI MASTER INITIALIZING ---\n");

  // Initialize SPI - Start conservatively at 1 MHz
  spi_init(SPI_PORT, 1000000);
  spi_set_slave(SPI_PORT, false);

  // Set communication Format (Mode 0 is most common)
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
  printf("SPI Clock: 1 MHz (Safe for most chips)\n");
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
  sleep_us(1);         // Small setup time
  int bytes_transferred = spi_write_read_blocking(spi, tx_buffer, rx_buffer,
                                                  len); // Full duplex transmit
  gpio_put(CS_PIN, 1);                                  // Comms down
  sleep_us(10);                                         // Recovery time
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
  uint32_t current_rx_offset = 0;

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

    // SFDP Header assembly
    if (command->opcode == 0x5A && command->rx_data_len == 24) {
      // Address bytes
      local_tx_buffer[1] = 0x08;
      local_tx_buffer[2] = 0x00;
      local_tx_buffer[3] = 0x00;
      // Dummies already set from memset above
    }

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

// Comprehensive JEDEC ID decoder
int decode_jedec_id(uint8_t mfr_id, uint8_t mem_type, uint8_t capacity) {
  // Reset Flash Info
  memset(&flash_info, 0, sizeof(flash_info));

  // Lookup manufacturer name
  const char *mfr_name = lookup_manufacturer(mfr_id);
  if (mfr_name) {
    strncpy(flash_info.manufacturer, mfr_name,
            sizeof(flash_info.manufacturer) - 1);
  } else {
    strncpy(flash_info.manufacturer, "Unknown",
            sizeof(flash_info.manufacturer) - 1);
  }

  // Temp Text, SFDP will update
  strncpy(flash_info.model, "Unknown", sizeof(flash_info.model) - 1);

  // ID Validity
  if ((mfr_id == 0xFF && mem_type == 0xFF && capacity == 0xFF) ||
      (mfr_id == 0x00 && mem_type == 0x00 && capacity == 0x00)) {
    return 0; // Invalid / no response
  }
  return 1; // Valid!
}
// Print JEDEC Report
void print_jedec_report(uint8_t mfr_id, uint8_t mem_type, uint8_t capacity) {
  print_section("JEDEC ID Analysis");

  printf("│ Raw bytes      : 0x%02X 0x%02X 0x%02X\n", mfr_id, mem_type,
         capacity);

  printf("│ Manufacturer   : %s\n", flash_info.manufacturer);
  printf("| Memory Type    : 0x%02X\n", mem_type);
  printf("| Capacity Byte  : 0x%02X\n", capacity);
  // Only show model line if you set it from SFDP previously
  if (flash_info.model[0] != '\0' && strcmp(flash_info.model, "Unknown") != 0) {
    printf("| Model          : %s\n", flash_info.model);
  }

  print_separator();
}
// Decode the SFDP Table
void decode_sfdp_header(const uint8_t *sfdp) {
  print_section("SFDP Header");

  bool valid =
      (sfdp[0] == 'S' && sfdp[1] == 'F' && sfdp[2] == 'D' && sfdp[3] == 'P');

  printf("│ Signature       : ");
  if (valid)
    printf("SFDP Success!\n");
  else {
    printf("SFDP Invalid!\n");
    print_separator();
    return;
  }

  uint8_t rev_minor = sfdp[4];
  uint8_t rev_major = sfdp[5];
  uint8_t hdr_count = sfdp[6] + 1; // per JEDEC: value+1 parameter headers
  uint8_t access_protocol = sfdp[7];

  printf("│ Revision            : %u.%u\n", rev_major, rev_minor);
  printf("│ Parameter Headers   : %u\n", hdr_count);
  printf("│ Access Protocol     : 0x%02X\n", access_protocol);

  print_separator();
}

// Decode SFDP Params Headers
void decode_sfdp_param_headers(const uint8_t *buf) {
  for (int i = 0; i < 3; i++) {
    const uint8_t *e = &buf[i * 8];

    uint16_t id = e[0] | (e[1] << 0);
    uint8_t rev = e[2];
    uint8_t len_dw = e[3];
    uint8_t ptr = (e[4] | (e[5] << 8) | (e[6] << 16));

    printf("│ Table %d\n", i + 1);
    printf("│   ID     : 0x%04X\n", id);
    printf("│   Rev    : 0x%02X\n", rev);
    printf("│   Length : %u DWORDs (%u bytes)\n", len_dw, len_dw * 4);
    printf("│   Ptr    : 0x%06lX\n", (unsigned long)ptr);
    printf("│\n");
  }

  print_separator();
}
