#ifndef HW_CONFIG_H
#define HW_CONFIG_H

#include "sd_card.h"

// Functions for SPI configuration (required by FatFS library)
size_t spi_get_num();
spi_t *spi_get_by_num(size_t num);

// Functions for SD card configuration (required by FatFS library)
size_t sd_get_num();
sd_card_t *sd_get_by_num(size_t num);

#endif
