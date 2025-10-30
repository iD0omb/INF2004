#include "functions.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"
#include <pico/stdio.h>
#include <stdint.h>
#include <stdio.h>

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

  printf("--- SPI MASTER CONFIGURATION COMPLETE ---");
}
// SPI Write to Slave -> Read from Slave
// int spi_command_transfer() {}
// Format Output to JSON
