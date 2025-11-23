#ifndef SPI_DIAG_H
#define SPI_DIAG_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

bool run_spi_diagnostic(char *json_out, size_t json_cap);
bool read_jedec_id(uint8_t *mfr, uint8_t *mem_type, uint8_t *capacity);

#endif // SPI_DIAG_H