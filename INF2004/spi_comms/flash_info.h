#ifndef FLASH_INFO_H
#define FLASH_INFO_H

#include <stdint.h>

typedef struct {
  char manufacturer[32];
  char model[32];
  uint32_t flash_size_bytes;
  uint32_t page_size_bytes;
  uint32_t sector_size_bytes;
  uint8_t quad_enable_supported;
  uint8_t qe_bit_pos;
  uint8_t source_sfdp_valid;
} flash_info_t;

// Global Flash info instance
extern flash_info_t flash_info;

#endif
