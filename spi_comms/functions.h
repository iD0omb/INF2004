#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <hardware/spi.h>
#include <stdint.h>

#define SPI_PORT spi0
#define MISO_PIN 4
#define MOSI_PIN 3
#define SCK_PIN 2
#define CS_PIN 5
#define LED 1

// Dummy opcodes for testing transmisison
typedef enum { master_msg = 0x01, slave_msg = 0x02 } opcode;

// Initalizes SPI
void spi_master_init(void);

// TX and RX 1 byte of data
int spi_command_transfer(spi_inst_t *spi, const uint8_t *tx_buffer,
                         uint8_t *rx_buffer, size_t len);
// Log transfer with "M"
void log_spi_tx(uint8_t sent, uint8_t received);

#endif
