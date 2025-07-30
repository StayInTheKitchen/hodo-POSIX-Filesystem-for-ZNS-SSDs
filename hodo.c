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
#include <linux/quotaops.h>

#include "zonefs.h"
#include "hodo.h"


// prototype: inode operations
static int hodo_setattr(struct mnt_idmap *idmap, struct dentry *dentry, struct iattr *iattr);
static struct dentry *hodo_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags);
static int hodo_create(struct mnt_idmap *idmap, struct inode *dir,struct dentry *dentry, umode_t mode, bool excl);

// global variable
struct hodo_mapping_info mapping_info;


// prototype : tool function
static struct dentry *hodo_sub_lookup(struct inode* dir, struct dentry* dentry, unsigned int flags);
uint64_t find_inode_number(struct hodo_inode *parent_hodo_inode, const char *target_name);
uint64_t find_inode_number_from_direct_block(
    const char *target_name,
    struct hodo_datablock *direct_block
);
uint64_t find_inode_number_from_indirect_block(
    const char *target_name,
    struct hodo_datablock *indirect_block
);
ssize_t hodo_read_struct(struct hodo_block_pos block_pos, char *out_buf, size_t len);
ssize_t hodo_write_struct(char *buf, size_t len);
ssize_t hodo_read_on_disk_mapping_info(void);
static int hodo_get_next_ino(void);

// init
void hodo_init() {
    ZONEFS_TRACE();
    hodo_read_on_disk_mapping_info();

    /*TO DO: crash check & recovery*/

    // 초기 상태: 포맷 이후 처음 마운트 된 경우
    if (mapping_info.wp.zone_id == 0 && mapping_info.wp.offset == 0) {        
        // 초기 wp는 zone 1번의 offset 0번
        mapping_info.wp.zone_id = 1;
        mapping_info.wp.offset = 0;

        // 시작 inode number는 1000
        mapping_info.starting_ino = hodo_nr_zones;
        pr_info("zonefs: starting_ino: %d\n", mapping_info.starting_ino);

        // root direcotry inode 설정
        struct hodo_inode root_inode;

        // 여기부터 hinode 초기화: 함수로 리팩터링
        root_inode.magic[0] = 'I';
        root_inode.magic[1] = 'N';
        root_inode.magic[2] = 'O';
        root_inode.magic[3] = 'D';

        root_inode.file_len = 0;

        root_inode.name_len = 1;
        memcpy(root_inode.name, "/", root_inode.name_len);

        root_inode.type = 1;

        root_inode.i_ino = hodo_get_next_ino();
        root_inode.i_mode = S_IFDIR; 

        root_inode.i_uid = current_fsuid();
        root_inode.i_gid = current_fsgid();

        root_inode.i_nlink = 1;

        // root inode를 wp에 쓰기
        hodo_write_struct((char*)&root_inode, sizeof(root_inode));

        mapping_info.mapping_table[root_inode.i_ino - mapping_info.starting_ino].zone_id = mapping_info.wp.zone_id; 
        mapping_info.mapping_table[root_inode.i_ino - mapping_info.starting_ino].offset = mapping_info.wp.offset;
    }
}

// file_operations -----------------------------------------------------------------------------------------------------
static int hodo_file_open(struct inode *inode, struct file *filp) {
    ZONEFS_TRACE();

    struct dentry *dentry = filp->f_path.dentry;
    const char *parent = dentry->d_parent->d_name.name;

    if (!strcmp(parent, "seq") || !strcmp(parent, "cnv")) {
        return zonefs_file_operations.open(inode, filp);
    }

    return 0;
}
static ssize_t hodo_read_dir(struct file *file, char __user *buf, size_t count, loff_t *pos) {
    ZONEFS_TRACE();

    return zonefs_dir_operations.read(file, buf, count, pos);
}

static int hodo_file_release(struct inode *inode, struct file *filp) {
    ZONEFS_TRACE();

    struct dentry *dentry = filp->f_path.dentry;
    const char *parent = dentry->d_parent->d_name.name;

    if (!strcmp(parent, "seq") || !strcmp(parent, "cnv")) {
        return zonefs_file_operations.release(inode, filp);
    }

    return 0;
}

static int hodo_file_fsync(struct file *filp, loff_t start, loff_t end, int datasync) {
    ZONEFS_TRACE();
    return zonefs_file_operations.fsync(filp, start, end, datasync);
}

static int hodo_file_mmap(struct file *filp, struct vm_area_struct *vma) {
    ZONEFS_TRACE();
    return zonefs_file_operations.mmap(filp, vma);
}

static loff_t hodo_file_llseek(struct file *filp, loff_t offset, int whence) {
    ZONEFS_TRACE();

    return zonefs_file_operations.llseek(filp, offset, whence);
}

static loff_t hodo_dir_llseek(struct file *filp, loff_t offset, int whence) {
    ZONEFS_TRACE();

    return zonefs_dir_operations.llseek(filp, offset, whence);
}

static ssize_t hodo_file_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	ZONEFS_TRACE();
	return zonefs_file_operations.read_iter(iocb, to);
}


static ssize_t hodo_file_write_iter(struct kiocb *iocb, struct iov_iter *from) {
    ZONEFS_TRACE();
    return zonefs_file_operations.write_iter(iocb, from);
}

static ssize_t hodo_file_splice_read(struct file *in, loff_t *ppos,
                                              struct pipe_inode_info *pipe, size_t len, unsigned int flags) {
    ZONEFS_TRACE();
    return zonefs_file_operations.splice_read(in, ppos, pipe, len, flags);
}

static ssize_t hodo_file_splice_write(struct pipe_inode_info *pipe, struct file *out,
                                             loff_t *ppos, size_t len, unsigned int flags) {
    ZONEFS_TRACE();
    return zonefs_file_operations.splice_write(pipe, out, ppos, len, flags);
}

static int hodo_file_iocb_bio_iopoll(struct kiocb *iocb, struct io_comp_batch *iob, unsigned int flags) {
    ZONEFS_TRACE();
    return zonefs_file_operations.iopoll(iocb, iob, flags);
}

static int hodo_readdir(struct file *file, struct dir_context *ctx) {
    ZONEFS_TRACE();
    return zonefs_dir_operations.iterate_shared(file, ctx);
}

const struct file_operations hodo_file_operations = {
	.open		= hodo_file_open,
	.release	= hodo_file_release,
	.fsync		= hodo_file_fsync,
	.mmap		= hodo_file_mmap,
	.llseek		= hodo_file_llseek,
	.read_iter	= hodo_file_read_iter,
	.write_iter	= hodo_file_write_iter,
	.splice_read	= hodo_file_splice_read,
	.splice_write	= hodo_file_splice_write,
	.iopoll		= hodo_file_iocb_bio_iopoll,
};

const struct file_operations hodo_dir_operations = {
	.llseek		= hodo_dir_llseek,
	.read		= hodo_read_dir,
	.iterate_shared	= hodo_readdir,
};

// inode operations------------------------------------------------------------------------------------------------------
static int hodo_setattr(struct mnt_idmap *idmap, struct dentry *dentry, struct iattr *iattr) {
    ZONEFS_TRACE();
    return zonefs_dir_inode_operations.setattr(idmap, dentry, iattr);
}

static struct dentry *hodo_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags) {
    ZONEFS_TRACE();

    const char* name = dentry->d_name.name;
    const char* parent = dentry->d_parent->d_name.name;

    //seq, cnv 또는 그 하위 디렉토리는 기존 zonefs lookup 사용
    if (!strcmp(name, "seq") || !strcmp(name, "cnv") ||
        !strcmp(parent, "seq") || !strcmp(parent, "cnv")) {
        pr_info("zonefs: using original lookup for '%s' (parent: %s)\n", name, parent);
        return zonefs_dir_inode_operations.lookup(dir, dentry, flags);
    }

    //그 외는 우리가 정의한 hodo sub lookup 사용
    pr_info("zonefs: using custom lookup for '%s' (parent: %s)\n", name, parent);
    return hodo_sub_lookup(dir, dentry, flags);
}

void hodo_set_bitmap(int i, int j) {
    mapping_info.bitmap[i] |= (1 << (31 - j));
}

static int hodo_get_next_ino(void) {
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

static int hodo_create(struct mnt_idmap *idmap, struct inode *dir,struct dentry *dentry, umode_t mode, bool excl) {
    ZONEFS_TRACE();

    struct inode *inode;
    struct timespec64 now;
    struct hodo_inode hinode;

    inode = new_inode(dir->i_sb);
    now = current_time(inode);  // inode의 superblock의 시간 정책에 따른 현재 시각

    // 여기부터 hinode 초기화: 함수로 리팩터링
    hinode.magic[0] = 'I';
    hinode.magic[1] = 'N';
    hinode.magic[2] = 'O';
    hinode.magic[3] = 'D';

    hinode.file_len = 0;

    hinode.name_len = dentry->d_name.len; 
    if (hinode.name_len > HODO_MAX_NAME_LEN) {
        // 구현해야 될 부분: error handling
    }
    memcpy(hinode.name, dentry->d_name.name, hinode.name_len);

    hinode.type = 0;

    // 구현해야 될 부분: hodo_get_next_ino();
    hinode.i_ino = hodo_get_next_ino();
    // 논의 사항: i_mode에 S_IFREG (regular file), S_IFDIR (directory) 두 개로 file인지 directory인지 구분이 가능함
    // 따라서, type 멤버 변수가 필요 없음.
    hinode.i_mode = S_IFREG | mode; 

    hinode.i_uid = current_fsuid();
    hinode.i_gid = current_fsgid();

    hinode.i_nlink = 1;

    hinode.i_atime = now;
    hinode.i_mtime = now;
    hinode.i_ctime = now;
    // 여기까지 hinode 초기화: 함수로 리팩터링

    inode->i_ino  = hinode.i_ino;
    inode->i_sb   = dir->i_sb;
    inode->i_op   = &hodo_file_inode_operations;
    inode->i_fop  = &hodo_file_operations;
    inode->i_mode = S_IFREG | mode;
    inode->i_uid  = current_fsuid();
    inode->i_gid  = current_fsgid();

    inode_set_ctime_to_ts(inode, now);
    inode_set_atime_to_ts(inode, now);
    inode_set_mtime_to_ts(inode, now);

    d_add(dentry, inode);

    hodo_write_struct((char*)&hinode, sizeof(struct hodo_inode));

    return 0;
}

const struct inode_operations hodo_file_inode_operations = {
    .setattr = hodo_setattr,
};

const struct inode_operations hodo_dir_inode_operations = {
    .lookup  = hodo_lookup,
    .setattr = hodo_setattr,
    .create = hodo_create,
};

// aops ---------------------------------------------------------------------------------------------------
static int hodo_read_folio(struct file *file, struct folio *folio) {
    ZONEFS_TRACE();
    return zonefs_file_aops.read_folio(file, folio);
}

static void hodo_readahead(struct readahead_control *rac) {
    ZONEFS_TRACE();
    zonefs_file_aops.readahead(rac);
}

static int hodo_writepages(struct address_space *mapping,
                                    struct writeback_control *wbc) {
    ZONEFS_TRACE();
    return zonefs_file_aops.writepages(mapping, wbc);
}

static bool hodo_dirty_folio(struct address_space *mapping, struct folio *folio) {
    ZONEFS_TRACE();
    return zonefs_file_aops.dirty_folio(mapping, folio);
}

static bool hodo_release_folio(struct folio *folio, gfp_t gfp) {
    ZONEFS_TRACE();
    return  zonefs_file_aops.release_folio(folio, gfp);
}

static void hodo_invalidate_folio(struct folio *folio, size_t offset, size_t length) {
    ZONEFS_TRACE();
    zonefs_file_aops.invalidate_folio(folio, offset, length);
}

static int hodo_migrate_folio(struct address_space *mapping,
                                        struct folio *dst, struct folio *src, enum migrate_mode mode) {
    ZONEFS_TRACE();
    return zonefs_file_aops.migrate_folio(mapping, dst, src, mode);
}

static bool hodo_is_partially_uptodate(struct folio *folio, size_t from, size_t count) {
    ZONEFS_TRACE();
    return zonefs_file_aops.is_partially_uptodate(folio, from, count);
}

static int hodo_error_remove_folio(struct address_space *mapping, struct folio *folio) {
    ZONEFS_TRACE();
    return zonefs_file_aops.error_remove_folio(mapping, folio);
}

static int hodo_swap_activate(struct swap_info_struct *sis, struct file *file,
                                       sector_t *span) {
    ZONEFS_TRACE();
    return zonefs_file_aops.swap_activate(sis, file, span);
}

const struct address_space_operations hodo_file_aops = {
    .read_folio            = hodo_read_folio,
    .readahead             = hodo_readahead,
    .writepages            = hodo_writepages,
    .dirty_folio           = hodo_dirty_folio,
    .release_folio         = hodo_release_folio,
    .invalidate_folio      = hodo_invalidate_folio,
    .migrate_folio         = hodo_migrate_folio,
    .is_partially_uptodate = hodo_is_partially_uptodate,
    .error_remove_folio    = hodo_error_remove_folio,
    .swap_activate         = hodo_swap_activate,
};

//tool_function_implementation----------------------------------------------------------------------------

static struct dentry *hodo_sub_lookup(struct inode* dir, struct dentry* dentry, unsigned int flags) {
    ZONEFS_TRACE();

    const char *name = dentry->d_name.name;
    const char *parent = dentry->d_parent->d_name.name;

    //부모 디렉토리의 hodo 아이노드를 읽어온다
    uint64_t parent_hodo_inode_number = dir->i_ino;
    struct hodo_block_pos parent_hodo_inode_pos = mapping_info.mapping_table[parent_hodo_inode_number - mapping_info.starting_ino];
    struct hodo_inode parent_hodo_inode = { 0, };
    hodo_read_struct(parent_hodo_inode_pos, (char*)&parent_hodo_inode, sizeof(struct hodo_inode));

    //찾고자 하는 이름을 가진 hodo 아이노드를 읽어온다
    uint64_t target_hodo_inode_number = find_inode_number(&parent_hodo_inode, name);
    struct hodo_block_pos target_hodo_inode_pos = mapping_info.mapping_table[target_hodo_inode_number - mapping_info.starting_ino];
    struct hodo_inode target_hodo_inode = { 0, };
    hodo_read_struct(target_hodo_inode_pos, (char*)&target_hodo_inode, sizeof(struct hodo_inode));

    //구한 hodo 아이노드 정보를 통해 VFS 아이노드를 구성하자
    struct inode *vfs_inode = new_inode(dir->i_sb);
    if (!vfs_inode)
        return ERR_PTR(-ENOMEM);

    vfs_inode->i_ino = target_hodo_inode.i_ino;
    vfs_inode->i_sb = dir->i_sb;
    vfs_inode->i_op = &hodo_dir_inode_operations;
    vfs_inode->i_fop = &hodo_file_operations;
    vfs_inode->i_mode = target_hodo_inode.i_mode;
    vfs_inode->i_uid = target_hodo_inode.i_uid;
    vfs_inode->i_gid = target_hodo_inode.i_gid;

    inode_set_ctime_to_ts(vfs_inode, target_hodo_inode.i_ctime);
    inode_set_mtime_to_ts(vfs_inode, target_hodo_inode.i_mtime);
    inode_set_atime_to_ts(vfs_inode, target_hodo_inode.i_atime);

    //찾고자 했던 VFS 아이노드를 VFS 덴트리에 이어주자
    d_add(dentry, vfs_inode);
    return dentry;
}

uint64_t find_inode_number(struct hodo_inode *parent_hodo_inode, const char *target_name) {
    uint64_t result = 0;
    
    //direct data block들에서 특정 이름의 hodo dentry를 찾아보기
    struct hodo_block_pos direct_block_pos = { 0, 0 };
    struct hodo_datablock *direct_block = kmalloc(4096, GFP_KERNEL);   //malloc은 사용자 공간에서만 사용가능하므로, 여기선 커널용인 kmalloc을 썼다.

    if (direct_block == NULL)
        pr_info("zonefs: (error in hodo_sub_lookup) cannot allocate 4KB heap space for datablock variable\n");

    for (int i = 0; i < 10; i++) {
        direct_block_pos = parent_hodo_inode->direct[i];
        hodo_read_struct(direct_block_pos, (char*)direct_block, 4096);

        result = find_inode_number_from_direct_block(target_name, direct_block);

        if (result != 0) {
            kfree(direct_block);
            return result;
        }
    }

    kfree(direct_block);

    //single indirect data block에서 특정 이름의 hodo dentry를 찾아보기
    struct hodo_block_pos single_indirect_block_pos = { 0, 0 };
    struct hodo_datablock *single_indirect_block = kmalloc(4096, GFP_KERNEL);

    if (single_indirect_block == NULL)
        pr_info("zonefs: (error in hodo_sub_lookup) cannot allocate 4KB heap space for datablock variable\n");

    single_indirect_block_pos = parent_hodo_inode->single_indirect;
    hodo_read_struct(single_indirect_block_pos, (char*)single_indirect_block, 4096);

    result = find_inode_number_from_indirect_block(target_name, single_indirect_block);

    kfree(single_indirect_block);

    if (result != 0)
        return result;


    //double indirect data block에서 특정 이름의 hodo dentry를 찾아보기
    struct hodo_block_pos double_indirect_block_pos = { 0, 0 };
    struct hodo_datablock *double_indirect_block = kmalloc(4096, GFP_KERNEL);

    if (double_indirect_block == NULL)
        pr_info("zonefs: (error in hodo_sub_lookup) cannot allocate 4KB heap space for datablock variable\n");

    double_indirect_block_pos = parent_hodo_inode->double_indirect;
    hodo_read_struct(double_indirect_block_pos, (char*)double_indirect_block, 4096);

    result = find_inode_number_from_indirect_block(target_name, double_indirect_block);

    kfree(double_indirect_block);

    if (result != 0) 
        return result;


    //triple indirect data block에서 특정 이름의 hodo dentry를 찾아보기
    struct hodo_block_pos triple_indirect_block_pos = { 0, 0 };
    struct hodo_datablock* triple_indirect_block = kmalloc(4096, GFP_KERNEL);

    if (triple_indirect_block == NULL)
        pr_info("zonefs: (error in hodo_sub_lookup) cannot allocate 4KB heap space for datablock variable\n");

    triple_indirect_block_pos = parent_hodo_inode->triple_indirect;
    hodo_read_struct(triple_indirect_block_pos, (char*)triple_indirect_block, 4096);

    result = find_inode_number_from_indirect_block(target_name, triple_indirect_block);

    kfree(triple_indirect_block);

    if (result != 0)
        return result;

    //모두 다 뒤져보았지만 찾는데 실패한 경우
    return 0;
}

uint64_t find_inode_number_from_direct_block(
    const char *target_name,
    struct hodo_datablock* direct_block
) {
    for (int j = 4; j < 4096 - sizeof(struct hodo_dentry); j += sizeof(struct hodo_dentry)) {
        struct hodo_dentry temp_dentry;
        memcpy(&temp_dentry, direct_block + j, sizeof(struct hodo_dentry));

        if (memcmp(temp_dentry.name, target_name, HODO_MAX_NAME_LEN))
            return temp_dentry.i_ino;
    }

    return 0;
}

uint64_t find_inode_number_from_indirect_block(
    const char *target_name,
    struct hodo_datablock *indirect_block
) {
    struct hodo_block_pos temp_block_pos;
    struct hodo_datablock *temp_block = kmalloc(4096, GFP_KERNEL);
    for (int j = 4; j < 4096 - sizeof(struct hodo_block_pos); j += sizeof(struct hodo_block_pos)) {
        memcpy(&temp_block_pos, indirect_block + j, sizeof(struct hodo_block_pos));
        hodo_read_struct(temp_block_pos, (char*)temp_block, 4096);

        uint64_t result = 0;

        if(indirect_block->magic[3] == '1')
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

ssize_t hodo_read_struct(struct hodo_block_pos block_pos, char *out_buf, size_t len)
{
    ZONEFS_TRACE();

    uint32_t zone_id = block_pos.zone_id;
    uint64_t offset = block_pos.offset;

    //읽을 양은 섹터(512Bytes) 단위여야 하고, 하나의 하나의 블럭 이내의 크기여야 한다.
    if (!out_buf || len == 0 || len > 4096 || (len % 512 != 0))
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