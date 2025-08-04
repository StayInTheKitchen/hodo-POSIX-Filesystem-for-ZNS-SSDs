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

#ifndef __TRANS_H__
#define __TRANS_H__

uint64_t find_inode_number(struct hodo_inode *parent_hodo_inode, const char *target_name);
uint64_t find_inode_number_from_direct_block(const char *target_name, struct hodo_datablock *direct_block);
uint64_t find_inode_number_from_indirect_block(const char *target_name,struct hodo_datablock *indirect_block);

int read_all_dirents(struct hodo_inode *dir_hodo_inode, struct dir_context *ctx, uint64_t *dirent_count);
int read_all_dirents_from_direct_block(struct hodo_datablock* direct_block,struct dir_context *ctx, uint64_t *dirent_count);
int read_all_dirents_from_indirect_block(struct hodo_datablock* indirect_block, struct dir_context *ctx, uint64_t *dirent_count);

int add_dirent(struct inode* dir, struct hodo_inode* sub_inode);

bool is_dirent_valid(struct hodo_dirent *dirent);
int hodo_get_next_ino(void);

ssize_t hodo_read_struct(struct hodo_block_pos block_pos, char *out_buf, size_t len);
ssize_t hodo_write_struct(char *buf, size_t len);
ssize_t hodo_read_on_disk_mapping_info(void);

bool is_block_pos_valid(struct hodo_block_pos block_pos);
bool is_directblock(struct hodo_datablock *datablock);
bool hodo_dir_emit(struct dir_context *ctx, struct hodo_dirent *temp_dirent);
#endif