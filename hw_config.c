#include "hw_config.h"

// SPI configuration for Maker Pi Pico built-in SD card
static spi_t spi1_config = {
    .hw_inst = spi1,        // NOTE: SPI1, not SPI0!
    .miso_gpio = 12,        // Changed from 16
    .mosi_gpio = 11,        // Changed from 19
    .sck_gpio = 10,         // Changed from 18
    .baud_rate = 400 * 1000,  // 400 kHz for initialization
    .set_drive_strength = true,
    .mosi_gpio_drive_strength = GPIO_DRIVE_STRENGTH_4MA,
    .sck_gpio_drive_strength = GPIO_DRIVE_STRENGTH_4MA,
};

// SD card configuration for Maker Pi Pico
static sd_card_t sd_card = {
    .pcName = "0:",
    .spi = &spi1_config,
    .ss_gpio = 15,          // Changed from 17 - CS on GP15
    .use_card_detect = true,  // Enable card detect
    .card_detect_gpio = 15,   // Card detect on GP15
    .card_detected_true = 1,  // Active high when card inserted
};

// SPI interface functions (required by FatFS)
size_t spi_get_num() {
    return 1;
}

spi_t *spi_get_by_num(size_t num) {
    if (0 == num) {
        return &spi1_config;
    }
    return NULL;
}

// SD card functions (required by FatFS)
size_t sd_get_num() {
    return 1;
}

sd_card_t *sd_get_by_num(size_t num) {
    if (0 == num) {
        return &sd_card;
    }
    return NULL;
}
