/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Simple zone file system for zoned block devices.
 *
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 *
 * Added POSIX features to original zonefs.
 *
 * Copyright (C) 2025 StayInTheKitchen, Antler9000
 */

#ifndef __HODO_H__
#define __HODO_H__

#define TB  (1ULL << 40)
#define GB  (1ULL << 30)
#define MB  (1ULL << 20)
#define KB  (1ULL << 10)
#define B   (1ULL << 00)

#define HODO_MAX_NAME_LEN   16

#define HODO_TYPE_REG       0               // regular file(ex : a.txt)      
#define HODO_TYPE_DIR       1               // directory file(ex : document)
#define END_READ            0               // for read_dir
#define NOTHING_FOUND       0               // for lookup, unlink
#define EMPTY_CHECKED       1               // for rmdir
#define NEW_DATABLOCK       0               // for write_struct

#define HODO_DATABLOCK_SIZE             4096 * B       
#define HODO_DATA_START                 8 * B
#define HODO_DATA_SIZE                  (HODO_DATABLOCK_SIZE - HODO_DATA_START)

#define NUMBER_ZONES                    16                              // 16 zones
#define ZONE_SIZE                       (256ULL * MB)                   // 256MB per each zone

#define SSD_CAPACITY                    (NUMBER_ZONES * ZONE_SIZE)              // total 4GB ZNS SSD
#define NUMBER_MAPPING_TABLE_ENTRY      (SSD_CAPACITY / HODO_DATABLOCK_SIZE)    // number of 2^20 entries
#define BLOCKS_PER_ZONE                 (ZONE_SIZE / HODO_DATABLOCK_SIZE)       // 

typedef unsigned int                    logical_block_number_t;
#define BLOCK_PTR_SZ                    sizeof(logical_block_number_t)

typedef uint32_t                        BITMAP_SECTOR;
#define BIT_PER_BYTE                    8
#define BITMAP_ENTRY_PER_SECTOR         (sizeof(uint32_t) * BIT_PER_BYTE)

#define ZONEFS_TRACE()                  pr_info("zonefs: >>> %s called\n", __func__)



struct hodo_block_pos {
    uint16_t zone_id;
    uint16_t block_index;
};

struct hodo_datablock {
    char magic[4];
    logical_block_number_t logical_block_number;
    char data[HODO_DATA_SIZE];
};

struct hodo_dirent {
    char name[HODO_MAX_NAME_LEN];
    uint8_t name_len;
    uint64_t i_ino;
    uint8_t file_type;
};

struct hodo_inode {
    char magic[4];
    uint64_t file_len;

    uint8_t  name_len;
    char     name[HODO_MAX_NAME_LEN];
    uint8_t  type;

    logical_block_number_t i_ino;
    umode_t  i_mode;
    kuid_t   i_uid;
    kgid_t   i_gid;
    unsigned int i_nlink;

    struct timespec64 i_atime;
    struct timespec64 i_mtime;
    struct timespec64 i_ctime;

    logical_block_number_t direct[10];
    logical_block_number_t single_indirect;
    logical_block_number_t double_indirect;
    logical_block_number_t triple_indirect;

    char padding[3936];
};

struct hodo_mapping_info {
    struct hodo_block_pos mapping_table[NUMBER_MAPPING_TABLE_ENTRY];
    logical_block_number_t starting_logical_number;
    struct hodo_block_pos wp;
    uint32_t logical_entry_bitmap[NUMBER_MAPPING_TABLE_ENTRY / 32];

    uint32_t invalid_count;
    uint32_t valid_count;
    uint32_t GC_bitmap[NUMBER_ZONES][BLOCKS_PER_ZONE / 32];
    struct hodo_block_pos swap_wp;
};

extern char mount_point_path[16];
extern struct hodo_mapping_info mapping_info;

void hodo_init(void);
#endif