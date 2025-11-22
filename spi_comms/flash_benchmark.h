/*
 * SPI Flash Benchmark Library
 * Comprehensive performance testing and forensic analysis
 */

#ifndef _FLASH_BENCHMARK_H
#define _FLASH_BENCHMARK_H

#include "pico.h"
#include "hardware/spi.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // Benchmark result structure matching CSV fields
    typedef struct
    {
        char chip_id[32];      // e.g., "Winbond_25Q32", "Unknown_ChipA"
        char operation[16];    // "read", "program", "erase"
        uint32_t block_size;   // Size in bytes
        uint32_t address;      // Memory offset
        uint64_t elapsed_us;   // Time in microseconds
        float throughput_MBps; // Derived throughput
        int run_number;        // Trial number
        float temp_C;          // Temperature during test
        float voltage_V;       // Supply voltage
        char pattern[16];      // Data pattern used
        char notes[64];        // Additional context
    } benchmark_result_t;

    // Core benchmark functions
    int flash_benchmark_init(void);
    int flash_identify_chip(char *chip_name, size_t name_size);
    uint64_t benchmark_flash_read(uint32_t address, uint32_t size, const char *pattern);
    uint64_t benchmark_flash_program(uint32_t address, uint32_t size, const char *pattern);
    uint64_t benchmark_flash_erase(uint32_t address, uint32_t size);

    // Pattern generation for different test scenarios
    void generate_test_pattern(uint8_t *buffer, uint32_t size, const char *pattern_type);

    // Flash chip low-level operations
    int flash_read_jedec_id(uint8_t *manufacturer, uint8_t *device_id_1, uint8_t *device_id_2);
    int flash_wait_busy(void);
    int flash_write_enable(void);
    int flash_sector_erase(uint32_t address);
    int flash_page_program(uint32_t address, const uint8_t *data, uint32_t size);
    int flash_read_data(uint32_t address, uint8_t *buffer, uint32_t size);

// Constants for flash operations
#define FLASH_CMD_READ_DATA 0x03
#define FLASH_CMD_FAST_READ 0x0B
#define FLASH_CMD_PAGE_PROGRAM 0x02
#define FLASH_CMD_SECTOR_ERASE 0x20
#define FLASH_CMD_BLOCK_ERASE_32K 0x52
#define FLASH_CMD_BLOCK_ERASE_64K 0xD8
#define FLASH_CMD_CHIP_ERASE 0xC7
#define FLASH_CMD_WRITE_ENABLE 0x06
#define FLASH_CMD_WRITE_DISABLE 0x04
#define FLASH_CMD_READ_STATUS 0x05
#define FLASH_CMD_JEDEC_ID 0x9F
#define FLASH_CMD_POWER_DOWN 0xB9
#define FLASH_CMD_POWER_UP 0xAB

#define FLASH_STATUS_BUSY 0x01
#define FLASH_STATUS_WEL 0x02

// Common flash chip specifications
#define FLASH_PAGE_SIZE 256
#define FLASH_SECTOR_SIZE 4096
#define FLASH_BLOCK_SIZE_32K 32768
#define FLASH_BLOCK_SIZE_64K 65536

#ifdef __cplusplus
}
#endif

#endif // _FLASH_BENCHMARK_H