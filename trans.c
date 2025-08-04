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

#include <linux/blkdev.h>
#include "zonefs.h"
#include "hodo.h"
#include "trans.h"

uint64_t find_inode_number(struct hodo_inode *parent_hodo_inode, const char *target_name) {
    uint64_t result = 0;
    struct hodo_datablock *buf_block = kmalloc(4096, GFP_KERNEL);   //malloc은 사용자 공간에서만 사용가능하므로, 여기선 커널용인 kmalloc을 썼다.

    if (buf_block == NULL) {
        pr_info("zonefs: (error in hodo_sub_lookup) cannot allocate 4KB heap space for datablock variable\n");
        return 0;
    }

    //direct data block들에서 특정 이름의 hodo dentry를 찾아보기
    struct hodo_block_pos direct_block_pos = { 0, 0 };

    for (int i = 0; i < 10; i++) {
        direct_block_pos = parent_hodo_inode->direct[i];
        
        if(direct_block_pos.zone_id == 0 && direct_block_pos.offset == 0)  
            continue;

        hodo_read_struct(direct_block_pos, (char*)buf_block, 4096);

        result = find_inode_number_from_direct_block(target_name, buf_block);

        if (result != 0) {
            kfree(buf_block);
            return result;
        }
    }

    //single indirect data block에서 특정 이름의 hodo dentry를 찾아보기
    struct hodo_block_pos single_indirect_block_pos = { 0, 0 };

    single_indirect_block_pos = parent_hodo_inode->single_indirect;

    if(single_indirect_block_pos.zone_id == 0 && single_indirect_block_pos.offset == 0) {}//볼 것 없으니 패스
    else {
        hodo_read_struct(single_indirect_block_pos, (char*)buf_block, 4096);

        result = find_inode_number_from_indirect_block(target_name, buf_block);

        if (result != 0) {
            kfree(buf_block);
            return result;
        }
    }

    //double indirect data block에서 특정 이름의 hodo dentry를 찾아보기
    struct hodo_block_pos double_indirect_block_pos = { 0, 0 };

    double_indirect_block_pos = parent_hodo_inode->double_indirect;

    if(double_indirect_block_pos.zone_id == 0 && double_indirect_block_pos.offset == 0) {}//볼 것 없으니 패스
    else {
        hodo_read_struct(double_indirect_block_pos, (char*)buf_block, 4096);

        result = find_inode_number_from_indirect_block(target_name, buf_block);

        if (result != 0) {
            kfree(buf_block);
            return result;
        }
    }

    //triple indirect data block에서 특정 이름의 hodo dentry를 찾아보기
    struct hodo_block_pos triple_indirect_block_pos = { 0, 0 };

    triple_indirect_block_pos = parent_hodo_inode->triple_indirect;

    if(triple_indirect_block_pos.zone_id == 0 && triple_indirect_block_pos.offset == 0) {}//볼 것 없으니 패스
    else {
        hodo_read_struct(triple_indirect_block_pos, (char*)buf_block, 4096);

        result = find_inode_number_from_indirect_block(target_name, buf_block);

        if (result != 0) {
            kfree(buf_block);
            return result;
        }
    }

    //모두 다 뒤져보았지만 찾는데 실패한 경우
    kfree(buf_block);
    return 0;
}

uint64_t find_inode_number_from_direct_block(
    const char *target_name,
    struct hodo_datablock* direct_block
) {
    for (int j = 4; j < 4096 - sizeof(struct hodo_dirent); j += sizeof(struct hodo_dirent)) {
        struct hodo_dirent temp_dirent;
        memcpy(&temp_dirent, direct_block + j, sizeof(struct hodo_dirent));

        if (memcmp(temp_dirent.name, target_name, HODO_MAX_NAME_LEN) == 0)
            return temp_dirent.i_ino;
    }

    return 0;
}

uint64_t find_inode_number_from_indirect_block(
    const char *target_name,
    struct hodo_datablock *indirect_block
) {
    struct hodo_block_pos temp_block_pos;
    struct hodo_datablock *temp_block = kmalloc(4096, GFP_KERNEL);

    if (temp_block == NULL) {
        pr_info("zonefs: (error in hodo_sub_lookup) cannot allocate 4KB heap space for datablock variable\n");
        return 0;
    }

    for (int j = 4; j < 4096 - sizeof(struct hodo_block_pos); j += sizeof(struct hodo_block_pos)) {
        memcpy(&temp_block_pos, indirect_block + j, sizeof(struct hodo_block_pos));

        if(temp_block_pos.zone_id == 0 && temp_block_pos.offset == 0)  
            continue;

        hodo_read_struct(temp_block_pos, (char*)temp_block, 4096);

        uint64_t result = 0;

        if(temp_block->magic[3] == '0')
            result = find_inode_number_from_direct_block(target_name, temp_block);

        else 
            result = find_inode_number_from_indirect_block(target_name, temp_block);

        if (result != 0){
            kfree(temp_block);
            return result;
        }
    }

    kfree(temp_block);
    return 0;
}

int read_all_dirents(
    struct hodo_inode *dir_hodo_inode, 
    struct file *file, struct dir_context *ctx, 
    uint64_t *dirent_count
) {
    ZONEFS_TRACE();
    struct hodo_datablock *buf_block = kmalloc(4096, GFP_KERNEL);   //malloc은 사용자 공간에서만 사용가능하므로, 여기선 커널용인 kmalloc을 썼다.

    if (buf_block == NULL) {
        pr_info("zonefs: (error in hodo_sub_readdir) cannot allocate 4KB heap space for datablock variable\n");
        return 0;
    }

    int result = 0;

    //direct data block들에서 hodo dirent들을 읽어내기
    struct hodo_block_pos direct_block_pos = { 0, 0 };

    for (int i = 0; i < 10; i++) {
        direct_block_pos = dir_hodo_inode->direct[i];
        
        if(direct_block_pos.zone_id == 0)  
            continue;

        pr_info("direct_block_pos: zoneid:%d\toffset:%d\n", direct_block_pos.zone_id, direct_block_pos.offset);
        hodo_read_struct(direct_block_pos, (char*)buf_block, 4096);
        pr_info("datablock!!!!!!\n%c%c%c%c\n", buf_block->magic[0],buf_block->magic[1],buf_block->magic[2],buf_block->magic[3]);

        result = read_all_dirents_from_direct_block(buf_block, file, ctx, dirent_count);
        
        if(result != 0)
            return result;
    }

    //single indirect data block에서 hodo dirent들을 읽어내기
    struct hodo_block_pos single_indirect_block_pos = { 0, 0 };

    single_indirect_block_pos = dir_hodo_inode->single_indirect;

    if(single_indirect_block_pos.zone_id == 0 && single_indirect_block_pos.offset == 0) {}//볼 것 없으니 패스
    else {
        hodo_read_struct(single_indirect_block_pos, (char*)buf_block, 4096);

        result = read_all_dirents_from_indirect_block(buf_block, file, ctx, dirent_count);

        if(result != 0)
            return result;
    }

    //double indirect data block에서 hodo dirent들을 읽어내기
    struct hodo_block_pos double_indirect_block_pos = { 0, 0 };

    double_indirect_block_pos = dir_hodo_inode->double_indirect;

    if(double_indirect_block_pos.zone_id == 0 && double_indirect_block_pos.offset == 0) {}//볼 것 없으니 패스
    else {
        hodo_read_struct(double_indirect_block_pos, (char*)buf_block, 4096);

        result = read_all_dirents_from_indirect_block(buf_block, file, ctx, dirent_count);

        if(result != 0)
            return result;
    }

    //triple indirect data block에서 hodo dirent들을 읽어내기
    struct hodo_block_pos triple_indirect_block_pos = { 0, 0 };

    triple_indirect_block_pos = dir_hodo_inode->triple_indirect;

    if(triple_indirect_block_pos.zone_id == 0 && triple_indirect_block_pos.offset == 0) {}//볼 것 없으니 패스
    else {
        hodo_read_struct(triple_indirect_block_pos, (char*)buf_block, 4096);

        result = read_all_dirents_from_indirect_block(buf_block, file, ctx, dirent_count);

        if(result != 0)
            return result;
    }

    //모두 잘 다 뒤져보았으므로 더 읽을 필요가 없음을 알려주기 위해 0를 반환하고 끝낸다
    kfree(buf_block);
    return 0;
}

int read_all_dirents_from_direct_block(
    struct hodo_datablock* direct_block,
    struct file *file, struct dir_context *ctx,
    uint64_t *dirent_count
) {
    ZONEFS_TRACE();
    for (int j = 4; j < 4096 - sizeof(struct hodo_dirent); j += sizeof(struct hodo_dirent)) {
        struct hodo_dirent temp_dirent;
        memcpy(&temp_dirent, (char*)direct_block + j, sizeof(struct hodo_dirent));
        
        pr_info("temp_dirent name: %s\n", temp_dirent.name);
        if(is_dirent_valid(&temp_dirent)) {
            pr_info("dirent_count:%d\tctx->pos:%d\n", *dirent_count, ctx->pos);
            if(*dirent_count == ctx->pos) {
                pr_info("1231231231231 dirent_count:%d\tctx->pos:%d\n", *dirent_count, ctx->pos);
                dir_emit(
                    ctx,
                    temp_dirent.name,
                    temp_dirent.name_len,
                    temp_dirent.i_ino,
                    ((temp_dirent.file_type == HODO_TYPE_DIR) ? DT_DIR : DT_REG)
                );
                pr_info("1231231231231 name:%s\n", temp_dirent.name);

                ctx->pos++;
            }

            (*dirent_count)++;
        }
    }

    return 0;
}

int read_all_dirents_from_indirect_block(
    struct hodo_datablock* indirect_block,
    struct file *file, struct dir_context *ctx,
    uint64_t *dirent_count
) {
    struct hodo_block_pos temp_block_pos;
    struct hodo_datablock *temp_block = kmalloc(4096, GFP_KERNEL);

     if (temp_block == NULL) {
        pr_info("zonefs: (error in hodo_sub_readdir) cannot allocate 4KB heap space for datablock variable\n");
        return 0;
    }

    for (int j = 4; j < 4096 - sizeof(struct hodo_block_pos); j += sizeof(struct hodo_block_pos)) {
        memcpy(&temp_block_pos, indirect_block + j, sizeof(struct hodo_block_pos));
        hodo_read_struct(temp_block_pos, (char*)temp_block, 4096);

        uint64_t result = 0;

        if(temp_block->magic[3] == '0')
            result = read_all_dirents_from_direct_block(temp_block, file, ctx, dirent_count);

        else 
            result = read_all_dirents_from_indirect_block(temp_block, file, ctx, dirent_count);

        if (result != 0){
            kfree(temp_block);
            return result;
        }
    }

    kfree(temp_block);
    return 0;
}

int add_dirent(struct inode* dir, struct hodo_inode* sub_inode) {
    ZONEFS_TRACE();

    // read directory inode
    struct hodo_block_pos dir_block_pos = mapping_info.mapping_table[dir->i_ino - mapping_info.starting_ino];
    struct hodo_inode dir_inode = {0,};

    hodo_read_struct(dir_block_pos, (char*)&dir_inode, sizeof(struct hodo_inode));

    struct hodo_datablock* temp_datablock = kmalloc(4096, GFP_KERNEL);

    for (int i = 0; i < 10; ++i) {
        if (dir_inode.direct[i].zone_id != 0) {
            struct hodo_block_pos temp_pos = {dir_inode.direct[i].zone_id, dir_inode.direct[i].offset};
            hodo_read_struct(temp_pos, (char*)temp_datablock, sizeof(struct hodo_datablock));

            // TODO: 지금은 direct block만 사용... indirect block도 사용하도록 수정
            for (int j = 4; j < 4096 - sizeof(struct hodo_dirent); j += sizeof(struct hodo_dirent)) {
                struct hodo_dirent temp_dirent;
                memcpy(&temp_dirent, (char*)temp_datablock + j, sizeof(struct hodo_dirent));
                
                if(is_dirent_valid(&temp_dirent)) {
                    continue; 
                }
                else {
                    memcpy(temp_dirent.name, sub_inode->name, sub_inode->name_len);
                    temp_dirent.name_len = sub_inode->name_len;
                    temp_dirent.i_ino = sub_inode->i_ino;
                    temp_dirent.file_type = sub_inode->type;

                    memcpy((char*)temp_datablock + j, &temp_dirent, sizeof(struct hodo_dirent));

                    dir_inode.file_len++;
                    dir_inode.direct[i].zone_id = mapping_info.wp.zone_id;
                    dir_inode.direct[i].offset = mapping_info.wp.offset;
                    hodo_write_struct((char*)temp_datablock, sizeof(struct hodo_datablock));

                    mapping_info.mapping_table[dir->i_ino - mapping_info.starting_ino].zone_id = mapping_info.wp.zone_id; 
                    mapping_info.mapping_table[dir->i_ino - mapping_info.starting_ino].offset = mapping_info.wp.offset; 
                    hodo_write_struct((char*)&dir_inode, sizeof(struct hodo_inode));

                    break;
                }
            }

            kfree(temp_datablock);
            return 0;
        }
        else {
            struct hodo_dirent temp_dirent;
            memcpy(temp_dirent.name, sub_inode->name, sub_inode->name_len);
            temp_dirent.name_len = sub_inode->name_len;
            temp_dirent.i_ino = sub_inode->i_ino;
            temp_dirent.file_type = sub_inode->type;

            temp_datablock->magic[0] = 'D';
            temp_datablock->magic[1] = 'A';
            temp_datablock->magic[2] = 'T';
            temp_datablock->magic[3] = '0';

            memcpy((char*)temp_datablock + 4, &temp_dirent, sizeof(struct hodo_dirent));

            dir_inode.file_len++;
            dir_inode.direct[i].zone_id = mapping_info.wp.zone_id;
            dir_inode.direct[i].offset = mapping_info.wp.offset;
            hodo_write_struct((char*)temp_datablock, sizeof(struct hodo_datablock));

            mapping_info.mapping_table[dir->i_ino - mapping_info.starting_ino].zone_id = mapping_info.wp.zone_id; 
            mapping_info.mapping_table[dir->i_ino - mapping_info.starting_ino].offset = mapping_info.wp.offset; 
            hodo_write_struct((char*)&dir_inode, sizeof(struct hodo_inode));

            kfree(temp_datablock);
            return 0;
        }
    }

    kfree(temp_datablock);
    return -1;
}

bool is_dirent_valid(struct hodo_dirent *dirent){
    ZONEFS_TRACE();
    pr_info("name:%s\tname_len%d\ti_ino:%d\tfile_type%d\n", dirent->name, dirent->name_len, dirent->i_ino, dirent->file_type);
    if(
        dirent->name[0] != '\0' &&
        dirent->name_len != 0 &&
        dirent->i_ino != 0 &&
        (dirent->file_type == HODO_TYPE_DIR ||
        dirent->file_type == HODO_TYPE_REG)
    )   
        return true;
    else
        return false;
}

void hodo_set_bitmap(int i, int j) {
    mapping_info.bitmap[i] |= (1 << (31 - j));
}

int hodo_get_next_ino(void) {
    for (int i = 0; i < (HODO_MAX_INODE / 32); ++i) {
        if (mapping_info.bitmap[i] != 0xFFFFFFFF) {
            for (int j = 0; j < 32; ++j) {
                if ((mapping_info.bitmap[i] & (1 << (31 - j))) == 0) {
                    hodo_set_bitmap(i, j);
                    return mapping_info.starting_ino + (i * 32 + j);
                }
            }
        }
    }
    return -1;
}

ssize_t hodo_read_struct(struct hodo_block_pos block_pos, char *out_buf, size_t len)
{
    ZONEFS_TRACE();

    uint32_t zone_id = block_pos.zone_id;
    uint64_t offset = block_pos.offset;

    //우리가 읽고 싶은 양은 0~4096 Byte의 범위안에서 512 Byte의 배수여야 한다
    if (!out_buf || len == 0 || len > 4096 || len % 512 != 0)
        return -EINVAL;

    //seq 파일을 열기 위해 경로 이름(path) 만들기
    const char path_up[16] = "/mnt/seq/";
    char path_down[6] = {0, };
    snprintf(path_down, sizeof(path_down), "%d", zone_id);
    const char *path = strncat(path_up, path_down, 6);

    struct file *zone_file;
    struct kiocb kiocb;
    struct iov_iter iter;
    struct kvec kvec;
    ssize_t ret;

    //파일 열기
    zone_file = filp_open(path, O_RDONLY | O_LARGEFILE, 0);
    if (IS_ERR(zone_file)) {
        pr_err("zonefs: filp_open(%s) failed\n", path);
        return PTR_ERR(zone_file);
    }

    //iov_iter 구성(읽을 버퍼들 여러개와 읽기 연산 등을 묶은 구조체)
    kvec.iov_base = out_buf;
    kvec.iov_len = len;
    iov_iter_kvec(&iter, ITER_DEST, &kvec, 1, len);

    //kiocb 구성(읽기 요청의 컨텍스트 정보)
    init_sync_kiocb(&kiocb, zone_file);
    kiocb.ki_pos = offset;

    //위 두 정보를 가지고 read_iter 실행
    if (!(zone_file->f_op) || !(zone_file->f_op->read_iter)) {
        pr_err("zonefs: read_iter not available on file\n");
        ret = -EINVAL;
        return ret;
    }

    ret = zone_file->f_op->read_iter(&kiocb, &iter);

    filp_close(zone_file, NULL);
    return ret;
}

ssize_t hodo_write_struct(char *buf, size_t len)
{
    ZONEFS_TRACE();

    uint32_t zone_id = mapping_info.wp.zone_id;
    uint64_t offset = mapping_info.wp.offset;

    //write size는 섹터(512Bytes) 단위여야 하고, 하나의 블럭 이내의 크기여야 한다.
    if (!buf || len == 0 || len > 4096 || (len % 512 != 0))
        return -EINVAL;

    if (offset + len > hodo_zone_size) {
        if (zone_id + 1 > hodo_nr_zones) {
            pr_err("device is full\n");
            return 0;
        }

        zone_id++;
        offset = 0;
    }

    //seq 파일을 열기 위해 경로 이름(path) 만들기
    const char path_up[16] = "/mnt/seq/";
    char path_down[6] = {0, };
    snprintf(path_down, sizeof(path_down), "%d", zone_id);
    const char *path = strncat(path_up, path_down, 6);

    struct file *zone_file;
    struct kiocb kiocb;
    struct iov_iter iter;
    struct kvec kvec;
    ssize_t ret;

    //파일 열기
    zone_file = filp_open(path, O_WRONLY | O_LARGEFILE, 0);
    if (IS_ERR(zone_file)) {
        pr_err("zonefs: filp_open(%s) failed\n", path);
        return PTR_ERR(zone_file);
    }

    //iov_iter 구성
    kvec.iov_base = (void*)buf;
    kvec.iov_len = len;
    iov_iter_kvec(&iter, ITER_SOURCE, &kvec, 1, len);

    //kiocb 구성
    init_sync_kiocb(&kiocb, zone_file);
    kiocb.ki_pos = offset;
    kiocb.ki_flags = IOCB_DIRECT;

    //위 두 정보를 가지고 write_iter 실행
    if (!(zone_file->f_op) || !(zone_file->f_op->write_iter)) {
         pr_err("zonefs: read_iter not available on file\n");
        ret = -EINVAL;
    }

    ret = zone_file->f_op->write_iter(&kiocb, &iter);

    filp_close(zone_file, NULL);

    if (offset + len == hodo_zone_size) {
        mapping_info.wp.zone_id = zone_id + 1; 
        mapping_info.wp.offset = 0;
    }
    else {
        mapping_info.wp.zone_id = zone_id; 
        mapping_info.wp.offset = offset + len;
    }

    return ret;
}

ssize_t hodo_read_on_disk_mapping_info(void)
{
    ZONEFS_TRACE();

    uint32_t zone_id = 0;
    uint64_t offset = 0;

    const char path_up[16] = "/mnt/seq/";
    char path_down[6] = {0, };
    snprintf(path_down, sizeof(path_down), "%d", zone_id);
    const char *path = strncat(path_up, path_down, 6);

    struct file *zone_file;
    struct kiocb kiocb;
    struct iov_iter iter;
    struct kvec kvec;
    ssize_t ret;

    //파일 열기
    zone_file = filp_open(path, O_RDONLY | O_LARGEFILE, 0);
    if (IS_ERR(zone_file)) {
        pr_err("zonefs: filp_open(%s) failed\n", path);
        return PTR_ERR(zone_file);
    }

    //iov_iter 구성(읽을 버퍼들 여러개와 읽기 연산 등을 묶은 구조체)
    kvec.iov_base = &mapping_info;
    kvec.iov_len = sizeof(struct hodo_mapping_info);
    iov_iter_kvec(&iter, ITER_DEST, &kvec, 1, sizeof(struct hodo_mapping_info));

    //kiocb 구성(읽기 요청의 컨텍스트 정보)
    init_sync_kiocb(&kiocb, zone_file);
    kiocb.ki_pos = offset;

    //위 두 정보를 가지고 read_iter 실행
    if (!(zone_file->f_op) || !(zone_file->f_op->read_iter)) {
         pr_err("zonefs: read_iter not available on file\n");
        ret = -EINVAL;
    }

    ret = zone_file->f_op->read_iter(&kiocb, &iter);

    filp_close(zone_file, NULL);
    return ret;
}