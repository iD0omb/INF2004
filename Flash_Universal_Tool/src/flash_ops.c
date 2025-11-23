#include "flash_ops.h"
#include "globals.h"
#include "spi_ops.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/time.h"
#include <stdio.h>

// ========== Flash Command Definitions ==========
#define FLASH_WRITE_ENABLE 0x06
#define FLASH_READ_STATUS 0x05
#define FLASH_READ_DATA 0x03
#define FLASH_PAGE_PROGRAM 0x02
#define FLASH_SECTOR_ERASE 0x20
#define FLASH_PAGE_SIZE 256
#define FLASH_SECTOR_SIZE 4096

// ========== Internal Flash Helpers ==========

// Poll status register until busy bit is cleared
static bool flash_wait_ready(uint32_t timeout_ms) {
    uint8_t status;
    uint8_t cmd = FLASH_READ_STATUS;
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);

    do {
        gpio_put(CS_PIN, 0); // CS Down
        spi_write_blocking(SPI_PORT, &cmd, 1);
        spi_read_blocking(SPI_PORT, 0xFF, &status, 1);
        gpio_put(CS_PIN, 1); // CS Up

        if (!(status & 0x01)) { // Check BUSY bit (Bit 0)
            return true;
        }
        sleep_us(100);
    } while (!time_reached(deadline));

    return false;
}

// Send Write Enable Latch command
static void flash_set_write_enable(void) {
    uint8_t cmd = FLASH_WRITE_ENABLE;
    gpio_put(CS_PIN, 0); // CS Down
    spi_write_blocking(SPI_PORT, &cmd, 1);
    gpio_put(CS_PIN, 1); // CS Up
}

// ========== Flash Operations Implementation ==========

bool flash_read_bytes(uint32_t address, uint8_t *buffer, size_t size) {
    if (!spi_initialized)
        return false;

    mutex_enter_blocking(&spi_mutex);

    uint8_t cmd_seq[4];
    cmd_seq[0] = FLASH_READ_DATA;
    cmd_seq[1] = (address >> 16) & 0xFF;
    cmd_seq[2] = (address >> 8) & 0xFF;
    cmd_seq[3] = address & 0xFF;

    gpio_put(CS_PIN, 0); // CS Down
    spi_write_blocking(SPI_PORT, cmd_seq, 4);
    spi_read_blocking(SPI_PORT, 0xFF, buffer, size);
    gpio_put(CS_PIN, 1); // CS Up

    mutex_exit(&spi_mutex);
    return true;
}

bool flash_erase_sector(uint32_t address) {
    if (!spi_initialized)
        return false;

    // Safety: Align to sector start
    address = address & ~(FLASH_SECTOR_SIZE - 1);

    mutex_enter_blocking(&spi_mutex);

    flash_set_write_enable();

    uint8_t cmd_seq[4];
    cmd_seq[0] = FLASH_SECTOR_ERASE;
    cmd_seq[1] = (address >> 16) & 0xFF;
    cmd_seq[2] = (address >> 8) & 0xFF;
    cmd_seq[3] = address & 0xFF;

    gpio_put(CS_PIN, 0); // CS Down
    spi_write_blocking(SPI_PORT, cmd_seq, 4);
    gpio_put(CS_PIN, 1); // CS Up

    // Sector erase can take 50ms to 400ms depending on chip
    bool result = flash_wait_ready(500);

    mutex_exit(&spi_mutex);
    return result;
}

bool flash_program_data(uint32_t addr, const uint8_t *data, size_t len) {
    if (!spi_initialized)
        return false;

    mutex_enter_blocking(&spi_mutex);

    uint32_t current_addr = addr;
    const uint8_t *current_ptr = data;
    size_t remaining_bytes = len;
    uint8_t cmd_seq[4];

    while (remaining_bytes > 0) {
        // Calculate remaining space in current page
        uint16_t page_offset = current_addr % FLASH_PAGE_SIZE;
        size_t space_in_page = FLASH_PAGE_SIZE - page_offset;
        size_t chunk_len =
            (remaining_bytes < space_in_page) ? remaining_bytes : space_in_page;

        flash_set_write_enable();

        cmd_seq[0] = FLASH_PAGE_PROGRAM;
        cmd_seq[1] = (current_addr >> 16) & 0xFF;
        cmd_seq[2] = (current_addr >> 8) & 0xFF;
        cmd_seq[3] = current_addr & 0xFF;

        gpio_put(CS_PIN, 0); // CS Down
        spi_write_blocking(SPI_PORT, cmd_seq, 4);
        spi_write_blocking(SPI_PORT, current_ptr, chunk_len);
        gpio_put(CS_PIN, 1); // CS Up

        if (!flash_wait_ready(50)) { // Page program usually < 3ms
            mutex_exit(&spi_mutex);
            printf("✗ Flash Write Timeout at 0x%06X\n", (unsigned int)current_addr);
            return false;
        }

        current_addr += chunk_len;
        current_ptr += chunk_len;
        remaining_bytes -= chunk_len;
    }

    mutex_exit(&spi_mutex);
    return true;
}