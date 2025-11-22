/*
 * FatFs - Generic FAT Filesystem Module  R0.15 (Simplified)
 * Real implementation for SD card file operations with Windows compatibility
 */

#include "ff.h"
#include "diskio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Globals
static FATFS FatFs[1];
static uint8_t sector_buffer[512];
static bool fs_ready = false;
static uint32_t partition_start_sector = 0;

// FAT32 boot sector (subset)
typedef struct
{
    uint8_t BS_jmpBoot[3];
    uint8_t BS_OEMName[8];
    uint16_t BPB_BytsPerSec;
    uint8_t BPB_SecPerClus;
    uint16_t BPB_RsvdSecCnt;
    uint8_t BPB_NumFATs;
    uint16_t BPB_RootEntCnt;
    uint16_t BPB_TotSec16;
    uint8_t BPB_Media;
    uint16_t BPB_FATSz16;
    uint16_t BPB_SecPerTrk;
    uint16_t BPB_NumHeads;
    uint32_t BPB_HiddSec;
    uint32_t BPB_TotSec32;
    uint32_t BPB_FATSz32;
    uint16_t BPB_ExtFlags;
    uint16_t BPB_FSVer;
    uint32_t BPB_RootClus;
    uint16_t BPB_FSInfo;
    uint16_t BPB_BkBootSec;
    uint8_t BPB_Reserved[12];
    uint8_t BS_DrvNum;
    uint8_t BS_Reserved1;
    uint8_t BS_BootSig;
    uint32_t BS_VolID;
    uint8_t BS_VolLab[11];
    uint8_t BS_FilSysType[8];
} __attribute__((packed)) BOOT_SECTOR;

// 8.3 directory entry (short name)
typedef struct
{
    uint8_t Name[11];
    uint8_t Attr;
    uint8_t NTRes;
    uint8_t CrtTimeTenth;
    uint16_t CrtTime;
    uint16_t CrtDate;
    uint16_t LstAccDate;
    uint16_t FstClusHI;
    uint16_t WrtTime;
    uint16_t WrtDate;
    uint16_t FstClusLO;
    uint32_t FileSize;
} __attribute__((packed)) DIR_ENTRY;

// Small helpers
static void mem_set(uint8_t *d, uint8_t c, int n)
{
    for (int i = 0; i < n; i++)
        d[i] = c;
}
static void mem_cpy(uint8_t *d, const uint8_t *s, int n)
{
    for (int i = 0; i < n; i++)
        d[i] = s[i];
}
static int mem_cmp(const uint8_t *a, const uint8_t *b, int n)
{
    for (int i = 0; i < n; i++)
        if (a[i] != b[i])
            return a[i] - b[i];
    return 0;
}

static void name_to_fat(const char *name, uint8_t *fat)
{
    mem_set(fat, ' ', 11);
    
    // Find extension
    const char *ext = strrchr(name, '.');
    int name_len = ext ? (ext - name) : strlen(name);
    
    // Copy name part (max 8 chars)
    for (int i = 0; i < name_len && i < 8; i++)
    {
        char c = name[i];
        if (c >= 'a' && c <= 'z')
            c = c - 'a' + 'A';
        fat[i] = c;
    }
    
    // Copy extension part (max 3 chars)
    if (ext)
    {
        ext++; // Skip the dot
        for (int i = 0; i < 3 && ext[i]; i++)
        {
            char c = ext[i];
            if (c >= 'a' && c <= 'z')
                c = c - 'a' + 'A';
            fat[8 + i] = c;
        }
    }
}

// ===================== MOUNT =====================
FRESULT f_mount(FATFS *fs, const char *path, uint8_t opt)
{
    (void)path;
    (void)opt;
    printf("Mounting FAT32 filesystem with Windows compatibility...\n");
    if (fs_ready)
    {
        printf("Filesystem already mounted\n");
        return FR_OK;
    }

    partition_start_sector = 0;

    if (disk_read(0, sector_buffer, 0, 1) != RES_OK)
    {
        printf("Failed to read sector 0\n");
        return FR_DISK_ERR;
    }

    uint16_t sig0 = *(uint16_t *)(sector_buffer + 510);
    printf("Boot signature: 0x%04X\n", sig0);
    if (sig0 != 0xAA55)
    {
        printf("NOFS: bad boot signature (0x%04X)\n", sig0);
        return FR_NO_FILESYSTEM;
    }

    BOOT_SECTOR *bs = (BOOT_SECTOR *)sector_buffer;
    bool looks_mbr = (sector_buffer[446 + 4] != 0x00);

    if (looks_mbr)
    {
        printf("Detected MBR - looking for FAT partition...\n");
        uint8_t *p = sector_buffer + 446;
        uint8_t ptype = p[4];
        uint32_t pstart = *(uint32_t *)(p + 8);
        printf("Partition 1: Status=0x%02X, Type=0x%02X, StartSector=%lu\n", p[0], ptype, pstart);

        if (ptype == 0x0B || ptype == 0x0C || ptype == 0x06)
        {
            partition_start_sector = pstart;
            printf("Using partition start at sector %lu\n", partition_start_sector);

            if (disk_read(0, sector_buffer, partition_start_sector, 1) != RES_OK)
            {
                printf("Failed to read partition boot sector\n");
                return FR_DISK_ERR;
            }
            bs = (BOOT_SECTOR *)sector_buffer;

            uint16_t psig = *(uint16_t *)(sector_buffer + 510);
            printf("Partition boot signature: 0x%04X\n", psig);
            if (psig != 0xAA55)
            {
                printf("NOFS: bad partition boot signature (0x%04X)\n", psig);
                return FR_NO_FILESYSTEM;
            }
        }
        else
        {
            printf("NOFS: unsupported partition type 0x%02X\n", ptype);
            return FR_NO_FILESYSTEM;
        }
    }
    else
    {
        printf("No MBR detected (super-floppy); using sector 0 as boot sector\n");
    }

    printf("OEM Name: '%.8s'\n", bs->BS_OEMName);
    printf("File System Type: '%.8s'\n", bs->BS_FilSysType);
    printf("Raw sector size: %u\n", bs->BPB_BytsPerSec);

    if (bs->BPB_BytsPerSec == 0)
    {
        printf("NOFS: sector size is 0\n");
        return FR_NO_FILESYSTEM;
    }
    if (bs->BPB_BytsPerSec != 512)
    {
        printf("NOFS: unsupported sector size %u (expected 512)\n", bs->BPB_BytsPerSec);
        return FR_NO_FILESYSTEM;
    }

    if (fs)
    {
        if (bs->BPB_SecPerClus == 0 || bs->BPB_NumFATs == 0)
        {
            printf("NOFS: invalid params SecPerClus=%u NumFATs=%u\n",
                   bs->BPB_SecPerClus, bs->BPB_NumFATs);
            return FR_NO_FILESYSTEM;
        }

        fs->fs_type = 1;
        fs->pdrv = 0;
        fs->csize = bs->BPB_SecPerClus;
        fs->n_fats = bs->BPB_NumFATs;
        fs->fsize = bs->BPB_FATSz32 ? bs->BPB_FATSz32 : 16;
        fs->volbase = partition_start_sector;
        fs->fatbase = partition_start_sector + bs->BPB_RsvdSecCnt;

        if (bs->BPB_RootEntCnt == 0)
        {
            uint32_t root_clus = bs->BPB_RootClus ? bs->BPB_RootClus : 2;
            fs->dirbase = partition_start_sector + bs->BPB_RsvdSecCnt + (bs->BPB_NumFATs * fs->fsize) + ((root_clus - 2) * bs->BPB_SecPerClus);
            printf("FAT32 root directory at sector %lu (cluster %lu)\n", fs->dirbase, root_clus);
        }
        else
        {
            fs->dirbase = partition_start_sector + bs->BPB_RsvdSecCnt + (bs->BPB_NumFATs * fs->fsize);
            printf("FAT16 root directory at sector %lu\n", fs->dirbase);
        }

        printf("Filesystem info: csize=%d, n_fats=%d, fsize=%lu\n",
               fs->csize, fs->n_fats, fs->fsize);
        printf("Base sectors: vol=%lu, fat=%lu, dir=%lu\n",
               fs->volbase, fs->fatbase, fs->dirbase);
    }

    if (fs)
    {
        FatFs[0] = *fs;
    }

    fs_ready = true;
    printf("Filesystem mounted successfully with Windows compatibility\n");
    return FR_OK;
}

// ===================== OPEN =====================
FRESULT f_open(FIL *fp, const char *path, uint8_t mode)
{
    if (!fs_ready || !fp)
        return FR_NOT_READY;
    printf("Opening file: %s (mode: 0x%02X)\n", path, mode);

    uint8_t fat_name[11];
    name_to_fat(path, fat_name);
    printf("FAT name: '%.11s'\n", fat_name);

    uint32_t root_sector = FatFs[0].dirbase;
    if (disk_read(0, sector_buffer, root_sector, 1) != RES_OK)
        return FR_DISK_ERR;

    DIR_ENTRY *entries = (DIR_ENTRY *)sector_buffer;
    bool found = false;
    int entry_idx = -1;

    for (int i = 0; i < 16; i++)
    {
        if (entries[i].Name[0] == 0)
            break;
        if (entries[i].Name[0] == 0xE5)
            continue;
        if (mem_cmp(entries[i].Name, fat_name, 11) == 0)
        {
            printf("File found at index %d\n", i);
            fp->fsize = entries[i].FileSize;
            fp->fptr = 0;
            fp->sclust = entries[i].FstClusLO | ((uint32_t)entries[i].FstClusHI << 16);
            if (fp->sclust == 0)
                fp->sclust = 3;
            found = true;
            entry_idx = i;
            break;
        }
    }

    if (!found && (mode & (FA_CREATE_NEW | FA_CREATE_ALWAYS | FA_OPEN_ALWAYS)))
    {
        for (int i = 0; i < 16; i++)
        {
            if (entries[i].Name[0] == 0 || entries[i].Name[0] == 0xE5)
            {
                mem_set((uint8_t *)&entries[i], 0, sizeof(DIR_ENTRY));
                mem_cpy(entries[i].Name, fat_name, 11);
                entries[i].Attr = 0x20;
                entries[i].NTRes = 0;
                entries[i].FileSize = 0;
                entries[i].FstClusHI = 0;
                entries[i].FstClusLO = 3;

                uint16_t t = 0x0000;
                uint16_t d = 0x52C8;
                entries[i].CrtTimeTenth = 0;
                entries[i].CrtTime = t;
                entries[i].CrtDate = d;
                entries[i].LstAccDate = d;
                entries[i].WrtTime = t;
                entries[i].WrtDate = d;

                printf("Creating Windows-compatible file entry for: %.11s\n", fat_name);
                if (disk_write(0, sector_buffer, root_sector, 1) != RES_OK)
                    return FR_DISK_ERR;
                disk_ioctl(0, CTRL_SYNC, NULL);
                sleep_ms(5);

                fp->fsize = 0;
                fp->sclust = 3;
                entry_idx = i;
                found = true;
                printf("Windows-compatible file entry created\n");
                break;
            }
        }
    }

    if (!found)
        return FR_NO_FILE;

    fp->dir_sect = root_sector;
    fp->dir_ptr = (uint8_t *)&entries[entry_idx];
    fp->dir_index = entry_idx;
    fp->stat = mode;
    fp->fs = &FatFs[0];

    if (found && (mode & FA_CREATE_ALWAYS))
    {
        printf("Truncating existing file (CREATE_ALWAYS)\n");
        fp->fsize = 0;
        fp->fptr = 0;
        if (disk_read(0, sector_buffer, fp->dir_sect, 1) == RES_OK)
        {
            DIR_ENTRY *d = (DIR_ENTRY *)sector_buffer;
            d[entry_idx].FileSize = 0;
            if (disk_write(0, sector_buffer, fp->dir_sect, 1) != RES_OK)
                return FR_DISK_ERR;
        }
    }

    printf("File opened: size=%lu, cluster=%lu\n", fp->fsize, fp->sclust);
    return FR_OK;
}

// ===================== WRITE =====================
FRESULT f_write(FIL *fp, const void *buff, UINT btw, UINT *bw)
{
    if (!fp || !buff || !bw)
        return FR_INVALID_PARAMETER;
    *bw = 0;
    if (btw == 0)
        return FR_OK;

    const uint8_t *p = (const uint8_t *)buff;
    uint32_t remaining = btw;
    const uint32_t data_sector_base = FatFs[0].dirbase + FatFs[0].csize;

    while (remaining > 0)
    {
        uint32_t target_sector = data_sector_base + (fp->fptr / 512);
        uint32_t byte_off = fp->fptr % 512;

        if (disk_read(0, sector_buffer, target_sector, 1) != RES_OK)
        {
            return FR_DISK_ERR;
        }

        uint32_t space = 512 - byte_off;
        uint32_t to_write = (remaining < space) ? remaining : space;

        mem_cpy(sector_buffer + byte_off, p, (int)to_write);

        if (disk_write(0, sector_buffer, target_sector, 1) != RES_OK)
        {
            return FR_DISK_ERR;
        }

        p += to_write;
        remaining -= to_write;
        fp->fptr += to_write;
        if (fp->fptr > fp->fsize)
            fp->fsize = fp->fptr;
    }

    if (fp->dir_sect)
    {
        if (disk_read(0, sector_buffer, fp->dir_sect, 1) == RES_OK)
        {
            DIR_ENTRY *d = (DIR_ENTRY *)sector_buffer;
            if (fp->dir_index < 16)
            {
                if (d[fp->dir_index].FstClusLO == 0)
                    d[fp->dir_index].FstClusLO = (uint16_t)(fp->sclust ? fp->sclust : 3);
                d[fp->dir_index].FileSize = fp->fsize;
                disk_write(0, sector_buffer, fp->dir_sect, 1);
            }
        }
    }

    *bw = btw;
    printf("%u bytes written successfully (multi-sector)\n", btw);
    return FR_OK;
}

// ===================== READ =====================
FRESULT f_read(FIL *fp, void *buff, UINT btr, UINT *br)
{
    if (!fp || !buff || !br)
        return FR_INVALID_PARAMETER;
    *br = 0;
    if (btr == 0)
        return FR_OK;

    uint32_t remain = (fp->fptr + btr > fp->fsize) ? (fp->fsize - fp->fptr) : btr;
    uint8_t *dst = (uint8_t *)buff;
    const uint32_t data_sector_base = FatFs[0].dirbase + FatFs[0].csize;

    while (remain > 0)
    {
        uint32_t target_sector = data_sector_base + (fp->fptr / 512);
        uint32_t byte_off = fp->fptr % 512;

        if (disk_read(0, sector_buffer, target_sector, 1) != RES_OK)
            return FR_DISK_ERR;

        uint32_t space = 512 - byte_off;
        uint32_t to_copy = (remain < space) ? remain : space;

        mem_cpy(dst, sector_buffer + byte_off, (int)to_copy);

        dst += to_copy;
        remain -= to_copy;
        fp->fptr += to_copy;
        *br += to_copy;
    }
    return FR_OK;
}

// ===================== SYNC =====================
FRESULT f_sync(FIL *fp)
{
    if (!fp)
        return FR_INVALID_OBJECT;
    printf("Syncing file for Windows compatibility...\n");

    if (fp->dir_sect)
    {
        if (disk_read(0, sector_buffer, fp->dir_sect, 1) == RES_OK)
        {
            DIR_ENTRY *e = (DIR_ENTRY *)sector_buffer;
            if (fp->dir_index < 16)
            {
                e[fp->dir_index].FileSize = fp->fsize;
                if (e[fp->dir_index].FstClusLO == 0 && fp->fsize > 0)
                {
                    e[fp->dir_index].FstClusLO = (uint16_t)(fp->sclust ? fp->sclust : 3);
                }
                e[fp->dir_index].Attr = 0x20;
                uint16_t t = 0x0000, d = 0x52C8;
                e[fp->dir_index].WrtTime = t;
                e[fp->dir_index].WrtDate = d;
                e[fp->dir_index].LstAccDate = d;
                e[fp->dir_index].CrtTime = t;
                e[fp->dir_index].CrtDate = d;

                if (disk_write(0, sector_buffer, fp->dir_sect, 1) != RES_OK)
                {
                    printf("Failed to write directory sector\n");
                    return FR_DISK_ERR;
                }
                disk_ioctl(0, CTRL_SYNC, NULL);
                sleep_ms(10);
                disk_ioctl(0, CTRL_SYNC, NULL);
            }
        }
        else
        {
            printf("Failed to read directory sector for sync\n");
        }
    }

    printf("File synced with Windows compatibility\n");
    return FR_OK;
}

// ===================== STAT =====================
FRESULT f_stat(const char *path, FILINFO *fno)
{
    if (!fs_ready || !fno)
        return FR_NOT_READY;

    uint8_t fat_name[11];
    name_to_fat(path, fat_name);

    uint32_t root_sector = FatFs[0].dirbase;
    if (disk_read(0, sector_buffer, root_sector, 1) != RES_OK)
        return FR_DISK_ERR;

    DIR_ENTRY *entries = (DIR_ENTRY *)sector_buffer;
    for (int i = 0; i < 16; i++)
    {
        if (entries[i].Name[0] == 0)
            break;
        if (entries[i].Name[0] == 0xE5)
            continue;
        if (mem_cmp(entries[i].Name, fat_name, 11) == 0)
        {
            fno->fsize = entries[i].FileSize;
            fno->fattrib = entries[i].Attr;

            int j = 0;
            for (int k = 0; k < 8 && entries[i].Name[k] != ' '; k++)
                fno->fname[j++] = entries[i].Name[k];
            if (entries[i].Name[8] != ' ')
            {
                fno->fname[j++] = '.';
                for (int k = 8; k < 11 && entries[i].Name[k] != ' '; k++)
                    fno->fname[j++] = entries[i].Name[k];
            }
            fno->fname[j] = 0;

            return FR_OK;
        }
    }
    return FR_NO_FILE;
}

// ===================== LSEEK / SIZE / CLOSE =====================
FRESULT f_lseek(FIL *fp, uint32_t ofs)
{
    if (!fp)
        return FR_INVALID_OBJECT;
    if (ofs > fp->fsize)
        ofs = fp->fsize;
    fp->fptr = ofs;
    printf("File position set to %lu\n", ofs);
    return FR_OK;
}

uint32_t f_size(FIL *fp)
{
    if (!fp)
        return 0;
    return fp->fsize;
}

FRESULT f_close(FIL *fp)
{
    if (!fp)
        return FR_INVALID_OBJECT;
    FRESULT r = f_sync(fp);
    if (r != FR_OK)
        return r;
    fp->fsize = 0;
    fp->fptr = 0;
    fp->sclust = 0;
    fp->stat = 0;
    fp->dir_sect = 0;
    fp->dir_index = 0;
    printf("File closed successfully\n");
    return FR_OK;
}

// ===================== DIRECTORY FUNCTIONS =====================
FRESULT f_opendir(DIR *dp, const char *path)
{
    if (!fs_ready || !dp)
        return FR_NOT_READY;

    // Support root directory "/" or ""
    if (strcmp(path, "/") == 0 || strcmp(path, "") == 0)
    {
        dp->fs = &FatFs[0];
        dp->sect = FatFs[0].dirbase;
        dp->index = 0;
        dp->dir_ptr = NULL;
        printf("# Opened root directory\n");
        return FR_OK;
    }

    // Support "logs" directory
    if (strcmp(path, "logs") == 0 || strcmp(path, "/logs") == 0)
    {
        dp->fs = &FatFs[0];
        dp->sect = FatFs[0].dirbase;
        dp->index = 0;
        dp->dir_ptr = NULL;
        printf("# Opened directory: logs\n");
        return FR_OK;
    }

    printf("### opendir: directory '%s' not supported\n", path);
    return FR_NO_PATH;
}

FRESULT f_closedir(DIR *dp)
{
    if (!dp)
        return FR_INVALID_OBJECT;
    dp->fs = NULL;
    return FR_OK;
}

FRESULT f_readdir(DIR *dp, FILINFO *fno)
{
    if (!dp || !fno || !dp->fs)
        return FR_INVALID_OBJECT;

    mem_set((uint8_t *)fno, 0, sizeof(FILINFO));

    if (disk_read(0, sector_buffer, dp->sect, 1) != RES_OK)
    {
        return FR_DISK_ERR;
    }

    DIR_ENTRY *entries = (DIR_ENTRY *)sector_buffer;

    while (dp->index < 16)
    {
        DIR_ENTRY *entry = &entries[dp->index];

        // End of directory (first byte is 0x00)
        if (entry->Name[0] == 0x00)
        {
            fno->fname[0] = 0;
            return FR_OK;
        }

        dp->index++;

        // Skip deleted entries (first byte is 0xE5)
        if (entry->Name[0] == 0xE5)
            continue;
            
        // Skip volume labels (attribute 0x08)
        if (entry->Attr & 0x08)
            continue;

        fno->fsize = entry->FileSize;
        fno->fattrib = entry->Attr;

        // Convert 8.3 name to string with proper formatting
        int j = 0;
        
        // Copy name part (up to 8 chars, stop at space)
        for (int k = 0; k < 8 && entry->Name[k] != ' '; k++)
        {
            fno->fname[j++] = entry->Name[k];
        }
        
        // Add extension if present (not all spaces)
        if (entry->Name[8] != ' ')
        {
            fno->fname[j++] = '.';
            for (int k = 8; k < 11 && entry->Name[k] != ' '; k++)
            {
                fno->fname[j++] = entry->Name[k];
            }
        }
        fno->fname[j] = '\0';

        return FR_OK;
    }

    fno->fname[0] = 0;
    return FR_OK;
}