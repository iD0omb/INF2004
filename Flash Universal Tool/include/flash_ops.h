#ifndef FLASH_OPS_H
#define FLASH_OPS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

bool flash_read_bytes(uint32_t address, uint8_t *buffer, size_t size);
bool flash_erase_sector(uint32_t address);
bool flash_program_data(uint32_t addr, const uint8_t *data, size_t len);

#endif // FLASH_OPS_H