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

#define HODO_MAX_NAME_LEN   16
#define HODO_MAX_INODE      (1 << 16)

#define HODO_TYPE_DIR       1
#define HODO_TYPE_REG       2            

#define ZONEFS_TRACE() pr_info("zonefs: >>> %s called\n", __func__)

struct hodo_block_pos {
    uint32_t zone_id;
    uint64_t offset;
};

struct hodo_inode {
    char magic[4];
    uint64_t file_len;

    uint8_t  name_len;
    char     name[HODO_MAX_NAME_LEN];
    uint8_t  type; // 0: file, 1: directory

    uint64_t i_ino;
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

    char padding[192];
};

struct hodo_datablock {
    char magic[4];
    char data[4092];
};

struct hodo_mapping_info {
    struct hodo_block_pos mapping_table[HODO_MAX_INODE];
    int starting_ino;
    struct hodo_block_pos wp;
    uint32_t bitmap[HODO_MAX_INODE / 32];
};

extern struct hodo_mapping_info mapping_info;

struct hodo_dirent {
    char name[HODO_MAX_NAME_LEN];
    uint8_t name_len;
    uint64_t i_ino;
    uint8_t file_type;
};

// prototype: hodo filesystem initialization
void hodo_init(void);
#endif