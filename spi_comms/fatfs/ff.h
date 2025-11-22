/*
 * FatFs - Generic FAT Filesystem Module  R0.15
 * Header file for embedded systems
 */

#ifndef FF_H
#define FF_H

#include <stdint.h>
#include <stddef.h>

// Integer type definitions for FatFs
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned long DWORD;
typedef unsigned long long QWORD;
typedef WORD WCHAR;
typedef DWORD LBA_t;

// FatFs return codes
typedef enum
{
    FR_OK = 0,              /* No error */
    FR_DISK_ERR,            /* A hard error occurred in the low level disk I/O layer */
    FR_INT_ERR,             /* Assertion failed */
    FR_NOT_READY,           /* The physical drive cannot work */
    FR_NO_FILE,             /* Could not find the file */
    FR_NO_PATH,             /* Could not find the path */
    FR_INVALID_NAME,        /* The path name format is invalid */
    FR_DENIED,              /* Access denied due to prohibited access or directory full */
    FR_EXIST,               /* Access denied due to prohibited access */
    FR_INVALID_OBJECT,      /* The file/directory object is invalid */
    FR_WRITE_PROTECTED,     /* The physical drive is write protected */
    FR_INVALID_DRIVE,       /* The logical drive number is invalid */
    FR_NOT_ENABLED,         /* The volume has no work area */
    FR_NO_FILESYSTEM,       /* There is no valid FAT volume */
    FR_MKFS_ABORTED,        /* The f_mkfs() aborted due to a parameter error */
    FR_TIMEOUT,             /* Could not get a grant to access the volume within defined period */
    FR_LOCKED,              /* The operation is rejected according to the file sharing policy */
    FR_NOT_ENOUGH_CORE,     /* LFN working buffer could not be allocated */
    FR_TOO_MANY_OPEN_FILES, /* Number of open files > _FS_LOCK */
    FR_INVALID_PARAMETER    /* Given parameter is invalid */
} FRESULT;

// File access mode and open method flags
#define FA_READ 0x01
#define FA_WRITE 0x02
#define FA_OPEN_EXISTING 0x00
#define FA_CREATE_NEW 0x04
#define FA_CREATE_ALWAYS 0x08
#define FA_OPEN_ALWAYS 0x10
#define FA_OPEN_APPEND 0x30

// File system object
typedef struct
{
    uint8_t fs_type;   /* File system type (0:invalid) */
    uint8_t pdrv;      /* Physical drive number */
    uint8_t ldrv;      /* Logical drive number (used only when _FS_REENTRANT) */
    uint8_t csize;     /* Cluster size [sectors] */
    uint32_t n_fats;   /* Number of FATs (1 or 2) */
    uint32_t fsize;    /* Sectors per FAT */
    uint32_t volbase;  /* Volume start sector */
    uint32_t fatbase;  /* FAT start sector */
    uint32_t dirbase;  /* Root directory start sector */
    uint32_t database; /* Data start sector */
    uint32_t winsect;  /* Current sector appearing in the win[] */
    uint8_t win[512];  /* Disk access window for Directory, FAT (and file data at tiny cfg) */
} FATFS;

// File object
typedef struct
{
    FATFS *fs;         /* Pointer to the related file system object */
    uint16_t id;       /* File system mount ID of the volume */
    uint8_t attr;      /* File attribute */
    uint8_t stat;      /* File status flags */
    uint32_t sclust;   /* File start cluster */
    uint32_t clust;    /* Current cluster of fpter */
    uint32_t sect;     /* Sector number appearing in buf[] */
    uint32_t dir_sect; /* Sector number containing the directory entry */
    uint8_t *dir_ptr;  /* Pointer to the directory entry in the win[] */
    uint8_t dir_index; /* Index of directory entry (0-15) */
    uint32_t fsize;    /* File size */
    uint32_t fptr;     /* File read/write pointer */
    uint8_t buf[512];  /* File private data read/write window */
} FIL;

// Directory object
typedef struct
{
    FATFS *fs;         /* Pointer to the owner file system object */
    uint32_t sect;     /* Current sector */
    uint16_t index;    /* Current index */
    uint8_t *dir_ptr;  /* Pointer to the directory entry in the win[] */
} DIR;

// File information structure
typedef struct
{
    uint32_t fsize;  /* File size */
    uint16_t fdate;  /* Modified date */
    uint16_t ftime;  /* Modified time */
    uint8_t fattrib; /* File attribute */
    char fname[13];  /* Short file name (8.3 format) */
} FILINFO;

typedef unsigned int UINT;

// Function declarations
FRESULT f_mount(FATFS *fs, const char *path, uint8_t opt);
FRESULT f_open(FIL *fp, const char *path, uint8_t mode);
FRESULT f_close(FIL *fp);
FRESULT f_read(FIL *fp, void *buff, UINT btr, UINT *br);
FRESULT f_write(FIL *fp, const void *buff, UINT btw, UINT *bw);
FRESULT f_sync(FIL *fp);
FRESULT f_stat(const char *path, FILINFO *fno);
FRESULT f_lseek(FIL *fp, uint32_t ofs);
uint32_t f_size(FIL *fp);

// Directory functions
FRESULT f_opendir(DIR *dp, const char *path);
FRESULT f_closedir(DIR *dp);
FRESULT f_readdir(DIR *dp, FILINFO *fno);

#endif /* FF_H */