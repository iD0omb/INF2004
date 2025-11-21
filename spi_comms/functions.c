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

    // JEDEC ID: must send only 1 byte (0x9F)
    {
        .opcode = 0x9F,
        .tx_len = 1,      // send only the opcode
        .rx_data_len = 3, // read 3 bytes (Mfr, Type, Capacity)
        .description = "JEDEC ID",
    },

    // Read Status Register-1
    {
        .opcode = 0x05,
        .tx_len = 1,      // send only the opcode
        .rx_data_len = 1, // read 1 status byte
        .description = "Read Status Register 1",
    },

    // Read Status Register-2
    {
        .opcode = 0x35,
        .tx_len = 1,
        .rx_data_len = 1,
        .description = "Read Status Register 2",
    },

    // Read Status Register-3
    {
        .opcode = 0x15,
        .tx_len = 1,
        .rx_data_len = 1,
        .description = "Read Status Register 3",
    },

    // Legacy Read Manufacturer / Device ID (0x90)
    // Format: 90h, dummy(3), addr(2), read(2)
    {
        .opcode = 0x90,
        .tx_len = 4,      // 1 opcode + 3 dummy bytes
        .rx_data_len = 2, // returns manufacturer + device ID
        .description = "Read Mfr/Device ID (Legacy)",
    },

    // Read Electronic Signature (0xAB)
    // Format: ABh then 3 dummy bytes, then read 1 byte
    {
        .opcode = 0xAB,
        .tx_len = 4,      // opcode + 3 dummy
        .rx_data_len = 1, // signature
        .description = "Read Electronic Signature",
    },

    // Read Unique ID (0x4B)
    // Format: 4Bh + 4 dummy bytes, then read 8 bytes
    {
        .opcode = 0x4B,
        .tx_len = 5,      // opcode + 4 dummy bytes
        .rx_data_len = 8, // 64-bit UID
        .description = "Read Unique ID (64-bit)",
    },

    // Read SFDP Header (0x5A)
    // Format: 5Ah + 3-byte address + dummy, then read
    {
        .opcode = 0x5A,
        .tx_len = 5,      // opcode + 3-byte address + 1 dummy
        .rx_data_len = 8, // read 8 bytes of header
        .description = "Read SFDP Header",
    },

    // Read SFDP Parameter Table (0x5A)
    {
        .opcode = 0x5A,
        .tx_len = 5,       // opcode + 3-byte address + 1 dummy
        .rx_data_len = 24, // read 24 bytes of parameter header
        .description = "Read SFDP Parameter Headers",
    }

};
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
  if (Opcode.tx_len == 0)
    return 0;
  // Send opcode only
  tx_buffer[0] = Opcode.opcode;
  gpio_put(CS_PIN, 0);
  spi_write_blocking(spi, tx_buffer, Opcode.tx_len);

  // Read full response
  spi_read_blocking(spi, 0x00, rx_buffer, Opcode.rx_data_len);
  gpio_put(CS_PIN, 1);

  return Opcode.rx_data_len;
}

// Transfer full SAFE Array block and write responses to the RX buffer

int spi_OPSAFE_transfer(spi_inst_t *spi, uint8_t *master_rx_buffer,
                        size_t max_report_len) {
  memset(master_rx_buffer, 0x00, max_report_len);

  size_t offset = 0;
  const size_t num_commands = sizeof(safeOps) / sizeof(safeOps[0]);
  size_t expected = get_expected_report_size();

  if (max_report_len < expected) {
    printf("ERROR: master buffer too small (%zu < %zu)\n", max_report_len,
           expected);
    return -1;
  }

  printf("Executing %zu safe commands...\n", num_commands);

  for (size_t i = 0; i < num_commands; i++) {
    const opcode *cmd = &safeOps[i];

    // --- Build TX buffer according to tx_len ---
    uint8_t tx[cmd->tx_len];
    memset(tx, 0x00, cmd->tx_len);
    tx[0] = cmd->opcode;

    // SFDP special case: parameter header (0x5A, 24 bytes)
    if (cmd->opcode == 0x5A && cmd->rx_data_len == 24) {
      tx[1] = 0x08; // address = 0x000008
      tx[2] = 0x00;
      tx[3] = 0x00;
      // tx[4] is dummy, already 0
    }

    // --- Do proper SPI: write THEN read ---
    gpio_put(CS_PIN, 0);
    spi_write_blocking(spi, tx, cmd->tx_len);

    uint8_t rx[cmd->rx_data_len];
    spi_read_blocking(spi, 0x00, rx, cmd->rx_data_len);
    gpio_put(CS_PIN, 1);

    // --- Store directly (no junk math) ---
    memcpy(&master_rx_buffer[offset], rx, cmd->rx_data_len);
    offset += cmd->rx_data_len;
  }

  return (int)offset;
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
