#include "functions.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"
#include <pico/stdio.h>
#include <pico/time.h>
#include <stdint.h>
#include <stdio.h>

#define SPI_PORT spi0
#define SCK_PIN 2
#define MOSI_PIN 3
#define MISO_PIN 4
#define CS_PIN 5

int main() {

  // Get Report size
  size_t report_size = get_expected_report_size();

  // Create Master RX Buffer
  uint8_t MASTER_RX_BUFFER[report_size];

  spi_master_init();

  for (;;) {
    sleep_ms(2000);

    uint8_t tx_buffer[4];
    uint8_t rx_buffer[4];

    printf("%zu Expected Report Size\n", get_expected_report_size());
    printf("Printing Response after ONE TRANSFER OF JEDEC ID OPCODE\n");
    const opcode chosen = safeOps[0];
    spi_ONE_transfer(SPI_PORT, chosen, tx_buffer, rx_buffer);
    for (size_t i = 0; i < sizeof(rx_buffer) / sizeof(rx_buffer[0]); i++) {
      printf("%X from RX BUFFER\n", rx_buffer[i]);
    }
  }
  return 0;
}
