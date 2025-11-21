#ifndef SD_CARD_H
#define SD_CARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// SD card interface using FatFs
bool sd_card_init(void);
bool sd_mount(void);
bool sd_write_file(const char *filename, const char *content);
bool sd_file_exists(const char *filename);
void sd_unmount(void);

#endif // SD_CARD_H