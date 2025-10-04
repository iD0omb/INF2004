#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"
#include <hardware/structs/io_bank0.h>
#include <pico/stdio.h>
#include <pico/time.h>
#include <stdint.h>
#include <stdio.h>

#define SPI_PORT spi0
#define SCK_PIN 2
#define MOSI_PIN 3
#define MISO_PIN 4
#define CS_PIN 5

#define MASTER_TX_BYTE 0xA1

int main() {
  // Run once, declarations etc.
  stdio_init_all();
  sleep_ms(2000);

  printf("--- SPI Master Initializing ---\n");

  // Initialize SPI
  spi_init(SPI_PORT, 100000);
  spi_set_slave(SPI_PORT, false);

  // Set SPI Format
  spi_set_format(SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

  // Initialize gpio
  gpio_set_function(SCK_PIN, GPIO_FUNC_SPI);
  gpio_set_function(MOSI_PIN, GPIO_FUNC_SPI);
  gpio_set_function(MISO_PIN, GPIO_FUNC_SPI);

  // Configure CS pin for manual control
  gpio_init(CS_PIN);
  gpio_set_dir(CS_PIN, GPIO_OUT);
  gpio_put(CS_PIN, 1);

  printf(" --- Master Configuration Complete -- \n");

  // Main loop
  for (;;) {
    // Buffers for spi_write_read function
    uint8_t tx_buf[1] = {MASTER_TX_BYTE};
    uint8_t rx_buf[1] = {0};

    // Drive CS Low to start transmission
    gpio_put(CS_PIN, 0);

    // Send MASTER_TX_BYTE through spi_write_read_blocking()
    spi_write_read_blocking(SPI_PORT, tx_buf, rx_buf, 1);

    // Drive CS High to end transmission
    gpio_put(CS_PIN, 1);

    // Output result of blocking transmission
    printf("[Master] SENT: 0x%02X, RECEIVED: 0x%02X\n", MASTER_TX_BYTE,
           rx_buf[0]);

    sleep_ms(1000);
  }
  return 0;
}
