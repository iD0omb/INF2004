#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <hardware/spi.h>
#include <stddef.h>
#include <stdint.h>

#define SPI_PORT spi0
#define MISO_PIN 4
#define MOSI_PIN 3
#define SCK_PIN 2
#define CS_PIN 5
#define LED 1

// Safe OPCODE Struct for mapping
typedef struct {
  uint8_t opcode;
  size_t tx_len;
  size_t rx_data_len;
  const char *description;
} opcode;

// Global struct array
extern const opcode safeOps[];

// Initalizes SPI
void spi_master_init(void);

// TX and RX of full OPSAFE block
int spi_OPSAFE_transfer(spi_inst_t *spi, uint8_t *master_rx_buffer,
                        size_t max_report_len);

// TX and RX of one specific opcode
int spi_ONE_transfer(spi_inst_t *spi, opcode Opcode, uint8_t *tx_buffer,
                     uint8_t *rx_buffer);

// TX helper function
int spi_transfer_block(spi_inst_t *spi, const uint8_t *tx_buffer,
                       uint8_t *rx_buffer, size_t len);

// Getter for expected Report Size
size_t get_expected_report_size(void);

// Getter for total number of commands in safeOps maps
size_t get_safe_command_count(void);

// Getter for pointer to a command struct from the map by index
const opcode *get_command_by_index(size_t index);

// JSON Formatting Function
#endif
