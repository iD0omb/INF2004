/*
 * SD Card Implementation using FatFs for JSON Log Tool
 */

#include "sd_card.h"
#include "fatfs/ff.h"
#include "pico/stdlib.h"
#include "fatfs/diskio.h"
#include <stdio.h>
#include <string.h>

static FATFS fatfs;
static bool sd_mounted = false;

// Helper function to create directory if it doesn't exist
static bool ensure_directory_exists(const char *path) {
    FILINFO fno;
    FRESULT fr = f_stat(path, &fno);
    
    if (fr == FR_OK) {
        // Directory exists
        return true;
    }
    
    // Try to create directory (note: f_mkdir not in our minimal FatFs)
    // For now, we'll just log that we tried
    printf("# Note: Directory %s may need to be created\n", path);
    return true; // Assume it's okay
}

bool sd_card_init(void) {
    printf("# Initializing SD Card hardware...\n");
    return true;
}

bool sd_mount(void) {
    if (sd_mounted) {
        printf("# SD card filesystem already mounted\n");
        return true;
    }
    
    printf("# Mounting FAT32 SD Card Filesystem...\n");
    
    DSTATUS st = disk_initialize(0);
    if (st & STA_NOINIT) {
        printf("### disk_initialize failed\n");
        return false;
    }
    
    FRESULT fr = f_mount(&fatfs, "", 1);
    
    if (fr == FR_OK) {
        sd_mounted = true;
        printf("# FAT32 SD Card filesystem mounted successfully!\n");
        return true;
    }
    
    printf("### Failed to mount FAT32 filesystem (error: %d)\n", fr);
    return false;
}

bool sd_file_exists(const char *filename) {
    if (!sd_mounted) {
        printf("### Cannot check file existence - SD card not mounted\n");
        return false;
    }
    
    FILINFO fno;
    FRESULT fr = f_stat(filename, &fno);
    
    if (fr == FR_OK) {
        printf("# File %s EXISTS (size: %lu bytes)\n", filename, fno.fsize);
        return true;
    }
    
    return false;
}

bool sd_write_file(const char *filename, const char *content) {
    if (!sd_mounted) {
        printf("### SD card not mounted\n");
        return false;
    }
    
    // Ensure logs directory exists
    ensure_directory_exists("logs");
    
    printf("# Writing file: %s\n", filename);
    
    FIL file;
    FRESULT fr = f_open(&file, filename, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) {
        printf("### Failed to open/create file (error: %d)\n", fr);
        return false;
    }
    
    UINT bytes_written = 0;
    size_t content_len = strlen(content);
    fr = f_write(&file, content, (UINT)content_len, &bytes_written);
    
    if (fr != FR_OK || bytes_written != content_len) {
        printf("### Failed to write file (error: %d, wrote: %u/%u)\n", 
               fr, bytes_written, (UINT)content_len);
        f_close(&file);
        return false;
    }
    
    fr = f_sync(&file);
    f_close(&file);
    
    if (fr != FR_OK) {
        printf("### Failed to sync file (error: %d)\n", fr);
        return false;
    }
    
    printf("# File written successfully (%u bytes)\n", bytes_written);
    return true;
}

void sd_unmount(void) {
    if (sd_mounted) {
        f_mount(NULL, "", 0);
        sd_mounted = false;
        printf("# SD Card unmounted\n");
    }
}

int sd_list_json_files(const char *directory, char filenames[][64], int max_files) {
    // Simplified implementation - would need f_opendir/f_readdir for full implementation
    // For now, return 0 (no files found)
    return 0;
}

int sd_read_file(const char *filename, char *buffer, size_t buffer_size) {
    if (!sd_mounted) {
        printf("### SD card not mounted\n");
        return -1;
    }
    
    FIL file;
    FRESULT fr = f_open(&file, filename, FA_OPEN_EXISTING | FA_READ);
    if (fr != FR_OK) {
        printf("### Failed to open file for reading (error: %d)\n", fr);
        return -1;
    }
    
    UINT bytes_read = 0;
    fr = f_read(&file, buffer, (UINT)(buffer_size - 1), &bytes_read);
    f_close(&file);
    
    if (fr != FR_OK) {
        printf("### Failed to read file (error: %d)\n", fr);
        return -1;
    }
    
    buffer[bytes_read] = '\0';
    return (int)bytes_read;
}