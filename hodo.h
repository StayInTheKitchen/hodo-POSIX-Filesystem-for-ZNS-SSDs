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

#define HODO_MAX_NAME_LEN       16
#define HODO_DATABLOCK_SIZE     4096
#define HODO_DATA_START         8
#define HODO_DATA_SIZE          (HODO_DATABLOCK_SIZE - HODO_DATA_START)

#define HODO_TYPE_REG           0            
#define HODO_TYPE_DIR           1

#define END_READ                0
#define NOTHING_FOUND           0
#define EMPTY_CHECKED           1

#define SSD_CAPACITY_GB                 4       // 4GB ZNS-SSD
#define NUMBER_MAPPING_TABLE_ENTRY      (SSD_CAPACITY_GB * (1 << 18)) 

#define NUMBER_ZONES            16              // 16 zones
#define ZONE_SIZE_MB            256             // 256MB per each zone
#define BLOCKS_PER_ZONE         (ZONE_SIZE_MB * (1 << 8))

#define ZONEFS_TRACE() pr_info("zonefs: >>> %s called\n", __func__)

typedef unsigned int logical_block_number_t;

// ZNS-SSD의 Physical Block 주소
struct hodo_block_pos {
    uint16_t zone_id;
    uint16_t block_index;
};

struct hodo_datablock {
    char magic[4];
    logical_block_number_t master_entry_num;
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

    struct hodo_block_pos direct[10];
    struct hodo_block_pos single_indirect;
    struct hodo_block_pos double_indirect;
    struct hodo_block_pos triple_indirect;

    // logical_block_number_t direct[10];
    // logical_block_number_t single_indirect;
    // logical_block_number_t double_indirect;
    // logical_block_number_t triple_indirect;

    char padding[3936];
};

struct hodo_mapping_info {
    struct hodo_block_pos mapping_table[NUMBER_MAPPING_TABLE_ENTRY];
    logical_block_number_t starting_logical_number;
    struct hodo_block_pos wp;
    uint32_t logical_entry_bitmap[NUMBER_MAPPING_TABLE_ENTRY / 32];
    uint32_t GC_bitmap[NUMBER_ZONES][BLOCKS_PER_ZONE / 32];
};

extern char mount_point_path[16];
extern struct hodo_mapping_info mapping_info;

void hodo_init(void);
#endif