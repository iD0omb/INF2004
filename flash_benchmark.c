/*
 * SPI Flash Benchmark Implementation
 * High-precision timing and forensic analysis of flash memory performance
 */

#include "flash_benchmark.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "pico/time.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Hardware configuration for flash chip (SPI0)
#define FLASH_SPI_INST spi0
#define FLASH_CS_PIN 17
#define FLASH_SCK_PIN 18
#define FLASH_MOSI_PIN 19
#define FLASH_MISO_PIN 16

// Global flash chip information
static bool flash_initialized = false;

// High-precision timing helpers
static inline uint64_t get_time_us(void)
{
    return to_us_since_boot(get_absolute_time());
}

// SPI communication helpers
static void flash_cs_select(void)
{
    gpio_put(FLASH_CS_PIN, 0);
    sleep_us(1);
}

static void flash_cs_deselect(void)
{
    sleep_us(1);
    gpio_put(FLASH_CS_PIN, 1);
}

static void flash_write_cmd(uint8_t cmd)
{
    spi_write_blocking(FLASH_SPI_INST, &cmd, 1);
}

static void flash_write_addr(uint32_t addr)
{
    uint8_t addr_bytes[3] = {
        (addr >> 16) & 0xFF,
        (addr >> 8) & 0xFF,
        addr & 0xFF};
    spi_write_blocking(FLASH_SPI_INST, addr_bytes, 3);
}

// Initialize flash benchmark system
int flash_benchmark_init(void)
{
    printf("🔧 Initializing Flash SPI interface...\n");

    // Initialize SPI0 at 8MHz (conservative speed for reliable operation)
    spi_init(FLASH_SPI_INST, 8000000);

    // Configure SPI pins
    gpio_set_function(FLASH_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(FLASH_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(FLASH_MISO_PIN, GPIO_FUNC_SPI);

    // Configure CS pin
    gpio_init(FLASH_CS_PIN);
    gpio_set_dir(FLASH_CS_PIN, GPIO_OUT);
    gpio_put(FLASH_CS_PIN, 1); // Deselect initially

    sleep_ms(10);

    // Try to read JEDEC ID to verify flash is present
    uint8_t manufacturer, device_id_1, device_id_2;
    if (flash_read_jedec_id(&manufacturer, &device_id_1, &device_id_2))
    {
        printf("Flash detected: Mfg=0x%02X, Dev=0x%02X%02X\n",
               manufacturer, device_id_1, device_id_2);
        flash_initialized = true;
        return 1;
    }
    else
    {
        printf("No flash chip detected\n");
        return 0;
    }
}

// Read JEDEC ID from flash chip
int flash_read_jedec_id(uint8_t *manufacturer, uint8_t *device_id_1, uint8_t *device_id_2)
{
    uint8_t id_data[3];

    flash_cs_select();
    flash_write_cmd(FLASH_CMD_JEDEC_ID);
    spi_read_blocking(FLASH_SPI_INST, 0xFF, id_data, 3);
    flash_cs_deselect();

    *manufacturer = id_data[0];
    *device_id_1 = id_data[1];
    *device_id_2 = id_data[2];

    // Check if we got valid data (not all 0xFF or 0x00)
    return (id_data[0] != 0xFF && id_data[0] != 0x00);
}

// Identify flash chip and return human-readable name
int flash_identify_chip(char *chip_name, size_t name_size)
{
    if (!flash_initialized)
    {
        strncpy(chip_name, "Unknown_Uninitialized", name_size - 1);
        chip_name[name_size - 1] = '\0';
        return 0;
    }

    uint8_t manufacturer, device_id_1, device_id_2;
    if (!flash_read_jedec_id(&manufacturer, &device_id_1, &device_id_2))
    {
        strncpy(chip_name, "Unknown_NoResponse", name_size - 1);
        chip_name[name_size - 1] = '\0';
        return 0;
    }

    // Simple chip identification based on JEDEC ID
    if (manufacturer == 0xEF)
    { // Winbond
        if (device_id_1 == 0x40 && device_id_2 == 0x16)
        {
            strncpy(chip_name, "Winbond_W25Q32", name_size - 1);
        }
        else if (device_id_1 == 0x40 && device_id_2 == 0x17)
        {
            strncpy(chip_name, "Winbond_W25Q64", name_size - 1);
        }
        else
        {
            snprintf(chip_name, name_size, "Winbond_Unknown_%02X%02X", device_id_1, device_id_2);
        }
    }
    else if (manufacturer == 0x20)
    { // Micron
        snprintf(chip_name, name_size, "Micron_%02X%02X", device_id_1, device_id_2);
    }
    else if (manufacturer == 0xC2)
    { // Macronix
        snprintf(chip_name, name_size, "Macronix_%02X%02X", device_id_1, device_id_2);
    }
    else if (manufacturer == 0x1F)
    { // Atmel/Microchip
        snprintf(chip_name, name_size, "Atmel_%02X%02X", device_id_1, device_id_2);
    }
    else
    {
        snprintf(chip_name, name_size, "Unknown_%02X_%02X%02X", manufacturer, device_id_1, device_id_2);
    }

    chip_name[name_size - 1] = '\0';
    return 1;
}

// Wait for flash chip to become ready
int flash_wait_busy(void)
{
    uint8_t status;
    int timeout = 10000; // 10 second timeout

    do
    {
        flash_cs_select();
        flash_write_cmd(FLASH_CMD_READ_STATUS);
        spi_read_blocking(FLASH_SPI_INST, 0xFF, &status, 1);
        flash_cs_deselect();

        if (!(status & FLASH_STATUS_BUSY))
        {
            return 1; // Not busy
        }

        sleep_us(100);
        timeout--;
    } while (timeout > 0);

    return 0; // Timeout
}

// Enable write operations
int flash_write_enable(void)
{
    flash_cs_select();
    flash_write_cmd(FLASH_CMD_WRITE_ENABLE);
    flash_cs_deselect();
    return 1;
}

// Read data from flash
int flash_read_data(uint32_t address, uint8_t *buffer, uint32_t size)
{
    flash_cs_select();
    flash_write_cmd(FLASH_CMD_READ_DATA);
    flash_write_addr(address);
    spi_read_blocking(FLASH_SPI_INST, 0xFF, buffer, size);
    flash_cs_deselect();
    return 1;
}

// Program a page of flash
int flash_page_program(uint32_t address, const uint8_t *data, uint32_t size)
{
    if (size > FLASH_PAGE_SIZE)
    {
        size = FLASH_PAGE_SIZE; // Limit to page size
    }

    flash_write_enable();

    flash_cs_select();
    flash_write_cmd(FLASH_CMD_PAGE_PROGRAM);
    flash_write_addr(address);
    spi_write_blocking(FLASH_SPI_INST, data, size);
    flash_cs_deselect();

    return flash_wait_busy();
}

// Erase a sector
int flash_sector_erase(uint32_t address)
{
    flash_write_enable();

    flash_cs_select();
    flash_write_cmd(FLASH_CMD_SECTOR_ERASE);
    flash_write_addr(address);
    flash_cs_deselect();

    return flash_wait_busy();
}

// Pattern generation functions
void generate_test_pattern(uint8_t *buffer, uint32_t size, const char *pattern_type)
{
    if (strcmp(pattern_type, "0xFF") == 0)
    {
        memset(buffer, 0xFF, size);
    }
    else if (strcmp(pattern_type, "0x00") == 0)
    {
        memset(buffer, 0x00, size);
    }
    else if (strcmp(pattern_type, "0x55") == 0)
    {
        memset(buffer, 0x55, size);
    }
    else if (strcmp(pattern_type, "random") == 0)
    {
        for (uint32_t i = 0; i < size; i++)
        {
            buffer[i] = rand() & 0xFF;
        }
    }
    else if (strcmp(pattern_type, "incremental") == 0)
    {
        for (uint32_t i = 0; i < size; i++)
        {
            buffer[i] = i & 0xFF;
        }
    }
    else
    {
        memset(buffer, 0xFF, size); // Default
    }
}

// High-precision benchmark functions
uint64_t benchmark_flash_read(uint32_t address, uint32_t size, const char *pattern)
{
    if (!flash_initialized)
        return 0;

    uint8_t *buffer = malloc(size);
    if (!buffer)
        return 0;

    printf("Reading %d bytes from 0x%06X... ", size, address);

    uint64_t start_time = get_time_us();

    // Perform the read operation
    flash_read_data(address, buffer, size);

    uint64_t end_time = get_time_us();
    uint64_t elapsed = end_time - start_time;

    printf("%.2f ms (%.2f MB/s)\n",
           elapsed / 1000.0,
           (size / 1024.0 / 1024.0) / (elapsed / 1000000.0));

    free(buffer);
    return elapsed;
}

uint64_t benchmark_flash_program(uint32_t address, uint32_t size, const char *pattern)
{
    if (!flash_initialized)
        return 0;

    uint8_t *buffer = malloc(size);
    if (!buffer)
        return 0;

    // Generate test pattern
    generate_test_pattern(buffer, size, pattern);

    printf("Programming %d bytes to 0x%06X with %s... ", size, address, pattern);

    uint64_t start_time = get_time_us();

    // Program in page-sized chunks
    uint32_t remaining = size;
    uint32_t current_addr = address;
    uint8_t *current_data = buffer;

    while (remaining > 0)
    {
        uint32_t chunk_size = (remaining > FLASH_PAGE_SIZE) ? FLASH_PAGE_SIZE : remaining;
        flash_page_program(current_addr, current_data, chunk_size);

        current_addr += chunk_size;
        current_data += chunk_size;
        remaining -= chunk_size;
    }

    uint64_t end_time = get_time_us();
    uint64_t elapsed = end_time - start_time;

    printf("%.2f ms (%.2f MB/s)\n",
           elapsed / 1000.0,
           (size / 1024.0 / 1024.0) / (elapsed / 1000000.0));

    free(buffer);
    return elapsed;
}

uint64_t benchmark_flash_erase(uint32_t address, uint32_t size)
{
    if (!flash_initialized)
        return 0;

    printf("Erasing %d bytes from 0x%06X... ", size, address);

    uint64_t start_time = get_time_us();

    // Erase in sector-sized chunks
    uint32_t remaining = size;
    uint32_t current_addr = address;

    while (remaining > 0)
    {
        flash_sector_erase(current_addr);
        current_addr += FLASH_SECTOR_SIZE;
        remaining = (remaining > FLASH_SECTOR_SIZE) ? (remaining - FLASH_SECTOR_SIZE) : 0;
    }

    uint64_t end_time = get_time_us();
    uint64_t elapsed = end_time - start_time;

    printf("%.2f ms\n", elapsed / 1000.0);

    return elapsed;
}