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

#define HDMR_MAX_NAME_LEN   16
#define HDMR_MAX_INODE      (1 << 16)

#define ZONEFS_TRACE() pr_info("zonefs: >>> %s called\n", __func__)

 // ---------- hdmr_structures ----------
struct hdmr_block_pos {
    uint32_t zone_id;
    uint64_t offset;
};

struct hdmr_datablock {
    char magic[4];
    char data[4092];
};

struct hdmr_inode {
    char magic[4];
    uint64_t file_len;

    uint8_t  name_len;
    char     name[HDMR_MAX_NAME_LEN];
    uint8_t  type;

    uint64_t i_ino;
    umode_t  i_mode;
    kuid_t   i_uid;
    kgid_t   i_gid;
    unsigned int i_nlink;

    struct timespec64 i_atime;
    struct timespec64 i_mtime;
    struct timespec64 i_ctime;

    struct hdmr_block_pos direct[10];
    struct hdmr_block_pos single_indirect;
    struct hdmr_block_pos double_indirect;
    struct hdmr_block_pos triple_indirect;
};

struct hdmr_mapping_info {
    struct hdmr_block_pos mapping_table[HDMR_MAX_INODE];
    int starting_ino;
    struct hdmr_block_pos wp;
    uint32_t bitmap[HDMR_MAX_INODE / 32];
} mapping_info;

struct hdmr_dentry {
    char name[HDMR_MAX_NAME_LEN];
    uint64_t i_ino;
};

// ---------- hdmr_sub_function_interface ----------

static struct dentry* hdmr_sub_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags);

// ---------- tool_function_interface ----------

uint64_t find_inode_number(struct hdmr_inode *parent_hdmr_inode, const char *target_name);

uint64_t find_inode_number_from_direct_block(
    const char *target_name,
    struct hdmr_datablock *direct_block
);

uint64_t find_inode_number_from_indirect_block(
    const char *target_name,
    struct hdmr_datablock *indirect_block
);

ssize_t hdmr_read_struct(struct hdmr_block_pos block_pos, char *out_buf, size_t len);

// ---------- file_operations ----------
static int hdmr_open(struct inode *inode, struct file *filp) {
    ZONEFS_TRACE();
    return zonefs_file_operations.open(inode, filp);
}

static int hdmr_release(struct inode *inode, struct file *filp) {
    ZONEFS_TRACE();
    return zonefs_file_operations.release(inode, filp);
}

static int hdmr_fsync(struct file *filp, loff_t start, loff_t end, int datasync) {
    ZONEFS_TRACE();
    return zonefs_file_operations.fsync(filp, start, end, datasync);
}

static int hdmr_mmap(struct file *filp, struct vm_area_struct *vma) {
    ZONEFS_TRACE();
    return zonefs_file_operations.mmap(filp, vma);
}

static loff_t hdmr_llseek(struct file *filp, loff_t offset, int whence) {
    ZONEFS_TRACE();
    return zonefs_file_operations.llseek(filp, offset, whence);
}

static ssize_t hdmr_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	ZONEFS_TRACE();
	return zonefs_file_operations.read_iter(iocb, to);
}


static ssize_t hdmr_write_iter(struct kiocb *iocb, struct iov_iter *from) {
    ZONEFS_TRACE();
    return zonefs_file_operations.write_iter(iocb, from);
}

static ssize_t hdmr_splice_read(struct file *in, loff_t *ppos,
                                              struct pipe_inode_info *pipe, size_t len, unsigned int flags) {
    ZONEFS_TRACE();
    return zonefs_file_operations.splice_read(in, ppos, pipe, len, flags);
}

static ssize_t hdmr_splice_write(struct pipe_inode_info *pipe, struct file *out,
                                             loff_t *ppos, size_t len, unsigned int flags) {
    ZONEFS_TRACE();
    return zonefs_file_operations.splice_write(pipe, out, ppos, len, flags);
}

static int hdmr_iocb_bio_iopoll(struct kiocb *iocb, struct io_comp_batch *iob, unsigned int flags) {
    ZONEFS_TRACE();
    return zonefs_file_operations.iopoll(iocb, iob, flags);
}

static int hdmr_readdir(struct file *file, struct dir_context *ctx) {
    ZONEFS_TRACE();
    return zonefs_dir_operations.iterate_shared(file, ctx);
}

const struct file_operations hdmr_file_operations = {
    .open           = hdmr_open,
    .release        = hdmr_release,
    .fsync          = hdmr_fsync,
    .mmap           = hdmr_mmap,
    .llseek         = hdmr_llseek,
    .read_iter      = hdmr_read_iter,
    .write_iter     = hdmr_write_iter,
    .splice_read    = hdmr_splice_read,
    .splice_write   = hdmr_splice_write,
    .iopoll         = hdmr_iocb_bio_iopoll,
    .iterate_shared = hdmr_readdir,
};


// ---------- inode operations ----------

static int hdmr_setattr(struct mnt_idmap *idmap, struct dentry *dentry, struct iattr *iattr) {
    ZONEFS_TRACE();
    return zonefs_dir_inode_operations.setattr(idmap, dentry, iattr);
}

static struct dentry *hdmr_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags) {
    ZONEFS_TRACE();

    const char* name = dentry->d_name.name;
    const char* parent = dentry->d_parent->d_name.name;

    //seq, cnv 또는 그 하위 디렉토리는 기존 zonefs lookup 사용
    if (!strcmp(name, "seq") || !strcmp(name, "cnv") ||
        !strcmp(parent, "seq") || !strcmp(parent, "cnv")) {
        pr_info("zonefs: using original lookup for '%s' (parent: %s)\n", name, parent);
        return zonefs_dir_inode_operations.lookup(dir, dentry, flags);
    }

    //그 외는 우리가 정의한 hdmr sub lookup 사용
    pr_info("zonefs: using custom lookup for '%s' (parent: %s)\n", name, parent);
    return hdmr_sub_lookup(dir, dentry, flags);
}

const struct inode_operations hdmr_inode_operations = {
    .lookup  = hdmr_lookup,
    .setattr = hdmr_setattr,
};

// ---------- aops ----------
static int hdmr_read_folio(struct file *file, struct folio *folio) {
    ZONEFS_TRACE();
    return zonefs_file_aops.read_folio(file, folio);
}

static void hdmr_readahead(struct readahead_control *rac) {
    ZONEFS_TRACE();
    zonefs_file_aops.readahead(rac);
}

static int hdmr_writepages(struct address_space *mapping,
                                    struct writeback_control *wbc) {
    ZONEFS_TRACE();
    return zonefs_file_aops.writepages(mapping, wbc);
}

static bool hdmr_dirty_folio(struct address_space *mapping, struct folio *folio) {
    ZONEFS_TRACE();
    return zonefs_file_aops.dirty_folio(mapping, folio);
}

static bool hdmr_release_folio(struct folio *folio, gfp_t gfp) {
    ZONEFS_TRACE();
    return  zonefs_file_aops.release_folio(folio, gfp);
}

static void hdmr_invalidate_folio(struct folio *folio, size_t offset, size_t length) {
    ZONEFS_TRACE();
    zonefs_file_aops.invalidate_folio(folio, offset, length);
}

static int hdmr_migrate_folio(struct address_space *mapping,
                                        struct folio *dst, struct folio *src, enum migrate_mode mode) {
    ZONEFS_TRACE();
    return zonefs_file_aops.migrate_folio(mapping, dst, src, mode);
}

static bool hdmr_is_partially_uptodate(struct folio *folio, size_t from, size_t count) {
    ZONEFS_TRACE();
    return zonefs_file_aops.is_partially_uptodate(folio, from, count);
}

static int hdmr_error_remove_folio(struct address_space *mapping, struct folio *folio) {
    ZONEFS_TRACE();
    return zonefs_file_aops.error_remove_folio(mapping, folio);
}

static int hdmr_swap_activate(struct swap_info_struct *sis, struct file *file,
                                       sector_t *span) {
    ZONEFS_TRACE();
    return zonefs_file_aops.swap_activate(sis, file, span);
}

const struct address_space_operations hdmr_file_aops = {
    .read_folio            = hdmr_read_folio,
    .readahead             = hdmr_readahead,
    .writepages            = hdmr_writepages,
    .dirty_folio           = hdmr_dirty_folio,
    .release_folio         = hdmr_release_folio,
    .invalidate_folio      = hdmr_invalidate_folio,
    .migrate_folio         = hdmr_migrate_folio,
    .is_partially_uptodate = hdmr_is_partially_uptodate,
    .error_remove_folio    = hdmr_error_remove_folio,
    .swap_activate         = hdmr_swap_activate,
};

// ---------- hdmr_sub_function_implementaion ----------

static struct dentry *hdmr_sub_lookup(struct inode* dir, struct dentry* dentry, unsigned int flags) {
    ZONEFS_TRACE();

    const char *name = dentry->d_name.name;
    const char *parent = dentry->d_parent->d_name.name;

    //부모 디렉토리의 hdmr 아이노드를 읽어온다
    uint64_t parent_hdmr_inode_number = dir->i_ino;
    struct hdmr_block_pos parent_hdmr_inode_pos = mapping_info.mapping_table[parent_hdmr_inode_number - mapping_info.starting_ino];
    struct hdmr_inode parent_hdmr_inode = { 0, };
    hdmr_read_struct(parent_hdmr_inode_pos, (char*)&parent_hdmr_inode, sizeof(struct hdmr_inode));

    //찾고자 하는 이름을 가진 hdmr 아이노드를 읽어온다
    uint64_t target_hdmr_inode_number = find_inode_number(&parent_hdmr_inode, name);
    struct hdmr_block_pos target_hdmr_inode_pos = mapping_info.mapping_table[target_hdmr_inode_number - mapping_info.starting_ino];
    struct hdmr_inode target_hdmr_inode = { 0, };
    hdmr_read_struct(target_hdmr_inode_pos, (char*)&target_hdmr_inode, sizeof(struct hdmr_inode));

    //구한 hdmr 아이노드 정보를 통해 VFS 아이노드를 구성하자
    struct inode *vfs_inode = new_inode(dir->i_sb);
    if (!vfs_inode)
        return ERR_PTR(-ENOMEM);

    vfs_inode->i_ino = target_hdmr_inode.i_ino;
    vfs_inode->i_sb = dir->i_sb;
    vfs_inode->i_op = &hdmr_inode_operations;
    vfs_inode->i_fop = &hdmr_file_operations;
    vfs_inode->i_mode = target_hdmr_inode.i_mode;
    vfs_inode->i_uid = target_hdmr_inode.i_uid;
    vfs_inode->i_gid = target_hdmr_inode.i_gid;

    inode_set_ctime_to_ts(vfs_inode, target_hdmr_inode.i_ctime);
    inode_set_mtime_to_ts(vfs_inode, target_hdmr_inode.i_mtime);
    inode_set_atime_to_ts(vfs_inode, target_hdmr_inode.i_atime);

    //찾고자 했던 VFS 아이노드를 VFS 덴트리에 이어주자
    d_add(dentry, vfs_inode);
    return dentry;
}

// ---------- tool_function_implementation ----------

uint64_t find_inode_number(struct hdmr_inode *parent_hdmr_inode, const char *target_name) {
    uint64_t result = 0;
    
    //direct data block들에서 특정 이름의 hdmr dentry를 찾아보기
    struct hdmr_block_pos direct_block_pos = { 0, 0 };
    struct hdmr_datablock *direct_block = kmalloc(4096, GFP_KERNEL);   //malloc은 사용자 공간에서만 사용가능하므로, 여기선 커널용인 kmalloc을 썼다.

    if (direct_block == NULL)
        pr_info("zonefs: (error in hdmr_sub_lookup) cannot allocate 4KB heap space for datablock variable\n");

    for (int i = 0; i < 10; i++) {
        direct_block_pos = parent_hdmr_inode->direct[i];
        hdmr_read_struct(direct_block_pos, (char*)direct_block, 4096);

        result = find_inode_number_from_direct_block(target_name, direct_block);

        if (result != 0) {
            kfree(direct_block);
            return result;
        }
    }

    kfree(direct_block);

    //single indirect data block에서 특정 이름의 hdmr dentry를 찾아보기
    struct hdmr_block_pos single_indirect_block_pos = { 0, 0 };
    struct hdmr_datablock *single_indirect_block = kmalloc(4096, GFP_KERNEL);

    if (single_indirect_block == NULL)
        pr_info("zonefs: (error in hdmr_sub_lookup) cannot allocate 4KB heap space for datablock variable\n");

    single_indirect_block_pos = parent_hdmr_inode->single_indirect;
    hdmr_read_struct(single_indirect_block_pos, (char*)single_indirect_block, 4096);

    result = find_inode_number_from_indirect_block(target_name, single_indirect_block);

    kfree(single_indirect_block);

    if (result != 0)
        return result;


    //double indirect data block에서 특정 이름의 hdmr dentry를 찾아보기
    struct hdmr_block_pos double_indirect_block_pos = { 0, 0 };
    struct hdmr_datablock *double_indirect_block = kmalloc(4096, GFP_KERNEL);

    if (double_indirect_block == NULL)
        pr_info("zonefs: (error in hdmr_sub_lookup) cannot allocate 4KB heap space for datablock variable\n");

    double_indirect_block_pos = parent_hdmr_inode->double_indirect;
    hdmr_read_struct(double_indirect_block_pos, (char*)double_indirect_block, 4096);

    result = find_inode_number_from_indirect_block(target_name, double_indirect_block);

    kfree(double_indirect_block);

    if (result != 0) 
        return result;


    //triple indirect data block에서 특정 이름의 hdmr dentry를 찾아보기
    struct hdmr_block_pos triple_indirect_block_pos = { 0, 0 };
    struct hdmr_datablock* triple_indirect_block = kmalloc(4096, GFP_KERNEL);

    if (triple_indirect_block == NULL)
        pr_info("zonefs: (error in hdmr_sub_lookup) cannot allocate 4KB heap space for datablock variable\n");

    triple_indirect_block_pos = parent_hdmr_inode->triple_indirect;
    hdmr_read_struct(triple_indirect_block_pos, (char*)triple_indirect_block, 4096);

    result = find_inode_number_from_indirect_block(target_name, triple_indirect_block);

    kfree(triple_indirect_block);

    if (result != 0)
        return result;

    //모두 다 뒤져보았지만 찾는데 실패한 경우
    return 0;
}

uint64_t find_inode_number_from_direct_block(
    const char *target_name,
    struct hdmr_datablock* direct_block
) {
    for (int j = 4; j < 4096 - sizeof(struct hdmr_dentry); j += sizeof(struct hdmr_dentry)) {
        struct hdmr_dentry temp_dentry;
        memcpy(&temp_dentry, direct_block + j, sizeof(struct hdmr_dentry));

        if (memcmp(temp_dentry.name, target_name, HDMR_MAX_NAME_LEN))
            return temp_dentry.i_ino;
    }

    return 0;
}

uint64_t find_inode_number_from_indirect_block(
    const char *target_name,
    struct hdmr_datablock *indirect_block
) {
    struct hdmr_block_pos temp_block_pos;
    struct hdmr_datablock *temp_block = kmalloc(4096, GFP_KERNEL);
    for (int j = 4; j < 4096 - sizeof(struct hdmr_block_pos); j += sizeof(struct hdmr_block_pos)) {
        memcpy(&temp_block_pos, indirect_block + j, sizeof(struct hdmr_block_pos));
        hdmr_read_struct(temp_block_pos, (char*)temp_block, 4096);

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

ssize_t hdmr_read_struct(struct hdmr_block_pos block_pos, char *out_buf, size_t len)
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