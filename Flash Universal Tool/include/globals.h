#ifndef GLOBALS_H
#define GLOBALS_H

#include <stdbool.h>
#include <stdint.h>
#include "pico/mutex.h"
#include "config.h" 

// ========== Global Variables (Defined in main.c) ==========

extern char json_buffer[JSON_BUFFER_SIZE];
extern char pico_ip_address[16];
extern bool sd_ready;
extern bool spi_initialized;
extern uint8_t last_jedec_id[3];

extern mutex_t spi_mutex;
extern mutex_t buffer_mutex;

#endif // GLOBALS_H