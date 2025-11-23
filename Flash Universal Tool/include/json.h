#ifndef JSON_H
#define JSON_H

#include <stddef.h>
#include <stdint.h>

// Generate full JSON using safeOps + decoded JEDEC
// Returns number of bytes written
// if return == 0, error or buffer is too small
size_t json_export_full_report(char *out_buf, size_t out_cap,
                               const uint8_t *report_buf, size_t report_len);

#endif
