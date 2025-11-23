#ifndef FLASH_DB_H
#define FLASH_DB_H

#include <stdint.h>

typedef struct {
  uint8_t id;
  const char *name;
} manufacturer_t;

const char *lookup_manufacturer(uint8_t id);

#endif
