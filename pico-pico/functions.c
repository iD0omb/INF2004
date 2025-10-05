#include "functions.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"
#include <pico/time.h>
#include <stdint.h>
#include <stdio.h>

// SPI Master Notes (RP2040)
// - CS pin must be controlled manually, Its a flag that indicates if
// communication is occuring
// - spi_init(spi,baudrate) enable the SPI* peripheral and sets the speed
// - gpio_set_function() maps GPIOs to SPI hardware
// Initalize the SPI
void spi_master_init(void) {}
