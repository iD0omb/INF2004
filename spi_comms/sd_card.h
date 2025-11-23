/* sd_card.h */
#ifndef SD_CARD_H
#define SD_CARD_H

#include <stdbool.h>
#include <stddef.h>

// Initialize Hardware, Filesystem, and Mutexes
bool sd_full_init(void);

// Check if mounted
bool sd_is_mounted(void);

// Thread-safe Read/Write wrappers
bool sd_write_safe(const char *filename, const char *data);
bool sd_read_safe(const char *filename, char *buffer, size_t buffer_size);

// --- Lower level functions (Internal use, but exposed if needed) ---
bool sd_card_init(void);
bool sd_mount(void);
bool sd_file_exists(const char *filename);
bool sd_write_file(const char *filename, const char *content);
int sd_read_file(const char *filename, char *buffer, size_t buffer_size);
void sd_unmount(void);

#endif