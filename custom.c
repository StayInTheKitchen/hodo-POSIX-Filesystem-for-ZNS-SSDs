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

    struct hdmr_datablock_pos direct[10];
    struct hdmr_datablock_pos single_indirect;
    struct hdmr_datablock_pos double_indirect;
    struct hdmr_datablock_pos triple_indirect;
};

struct hdmr_datablock {
    char magic[4];
    char data[4092];
};

struct hdmr_mapping_info {
    struct hdmr_datablock_pos mapping_table[HDMR_MAX_INODE];
    int starting_ino;
    struct hdmr_datablock_pos wp;
    uint32_t bitmap[HDMR_MAX_INODE / 32];
} global_mapping_info;

struct hdmr_dentry {
    char name[HDMR_MAX_NAME_LEN];
    uint64_t i_ino;
};

// ---------- hdmr_sub_function_interface ----------

static struct dentry* hdmr_sub_lookup(struct inode* dir, struct dentry* dentry, unsigned int flags);

// ---------- tool_function_interface ----------

uint64_t find_inode_number(struct hdmr_inode* parent_hdmr_inode);

uint64_t find_inode_number_in_direct_block(
    const char* target_name,
    hdmr_datablock* direct_block
);

uint64_t find_inode_number_in_single_indirect_block(
    const char* target_name,
    hdmr_datablock* single_indirect_block,
    hdmr_datablock* temp_direct_block
);

uint64_t find_inode_number_in_double_indirect_block(
    const char* target_name,
    hdmr_datablock* double_indirect_block,
    hdmr_datablock* temp_single_indirect_block,
    hdmr_datablock* temp_direct_block
);

uint64_t find_inode_number_in_triple_indirect_block(
    const char* target_name,
    hdmr_datablock* triple_indirect_block,
    hdmr_datablock* temp_double_indirect_block,
    hdmr_datablock* temp_single_indirect_block,
    hdmr_datablock* temp_direct_block
);

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

    //"seq", "cnv" 또는 그 하위 디렉토리는 기존 zonefs lookup 사용
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
    .create = hdmr_create,
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

static struct dentry* hdmr_sub_lookup(struct inode* dir, struct dentry* dentry, unsigned int flags) {
    ZONEFS_TRACE();

    const char* name = dentry->d_name.name;
    const char* parent = dentry->d_parent->d_name.name;

    //부모 디렉토리의 hdmr 아이노드를 읽어온다
    uint64_t parent_hdmr_inode_number = dir->i_ino;
    struct hdmr_block_pos parent_hdmr_inode_pos = global_mapping_info.mapping_table[parent_hdmr_inode_number - global_mapping_info.starting_ino];
    struct hdmr_inode parent_hdmr_inode = { 0, } //TODO : 저장장치의 parent_hdmr_inode_pos 위치로부터 아이노드 읽어오기

        //찾고자 하는 이름을 가진 hdmr 아이노드를 읽어온다
    uint64_t target_hdmr_inode_number = find_inode_number(&parent_hdmr_inode);
    struct hdmr_block_pos target_hdmr_inode_pos = global_mapping_info.mapping_table[target_hdmr_inode_number - global_mapping_info.starting_ino];
    struct hdmr_inode target_hdmr_inode = { 0, }; //TODO : 저장장치의 target_hdmr_inode_pos 위치로부터 아이노드 읽어오기

    //구한 hdmr 아이노드 정보를 통해 VFS 아이노드를 구성하자
    struct inode* vfs_inode = new_inode(dir->i_sb);
    if (!vfs_inode)
        return ERR_PTR(-ENOMEM);

    vfs_inode->i_ino = target_inode.i_ino;
    vfs_inode->i_sb = dir->i_sb;
    vfs_inode->i_op = &hdmr_inode_operations;
    inode->i_fop = &hdmr_file_operations;
    inode->i_mode = target_inode.i_mode;
    inode->i_uid = target_inode.i_uid;
    inode->i_gid = target_inode.i_gid;

    inode_set_ctime_to_ts(inode, target_Inode.i_ctime);
    inode_set_mtime_to_ts(inode, target_inode.i_mtime);
    inode_set_atime_to_ts(inode, target_inode.i_atime);

    //찾고자 했던 VFS 아이노드을 VFS 덴트리에 이어주자
    d_add(dentry, vfs_inode);
    return dentry;
}

// ---------- tool_function_implementation ----------

uint64_t find_inode_number(struct hdmr_inode* parent_hdmr_inode, const char* target_name) {
    //direct data block들에서 특정 이름의 dentry 찾아보기
    struct hdmr_block_pos direct_block_pos = { 0, 0 };
    struct hdmr_datablock* direct_block = kmalloc(4096, GFP_KERNEL);   //malloc은 사용자 공간에서만 사용가능하므로, 여기선 커널용인 kmalloc을 썼다

    if (temp_direct_block == nullptr)
        r_info("zonefs: (error in hdmr_sub_lookup) cannot allocate 4KB heap space for datablock variable\n");

    for (int i = 0; i < 10; i++) {
        direct_block_pos = parent_hdmr_inode->direct[i];
        direct_block;      //TODO : 저장장치의 direct_block_pos으로부터 4KB의 구간을 direct_block으로 읽어오기

        uint64_t result = find_inode_number_in_direct_block(target_name, direct_block);

        if (result != 0) {
            kfree(direct_block);
            return result;
        }
    }

    //single indirect data block에서 특정 이름의 dentry 찾아보기
    struct hdmr_block_pos single_indirect_block_pos = { 0, 0 };
    struct hdmr_datablock* single_indirect_block = kmalloc(4096, GFP_KERNEL);

    if (single_indirect_block == nullptr)
        r_info("zonefs: (error in hdmr_sub_lookup) cannot allocate 4KB heap space for datablock variable\n");

    single_indirect_block_pos = parent_hdmr_inode->single_indirect;
    single_indirect_block; //TODO : 저장장치의 single_indirect_block_pos으로부터 4KB의 구간을 single_indirect_block으로 읽어오기

    uint64_t result = find_inode_number_in_single_indirect_block(target_name, single_indirect_block, direct_block);

    if (result != 0) {
        kfree(single_indirect_block);
        kfree(direct_block);
        return result;
    }

    //double indirect data block에서 특정 이름의 dentry 찾아보기
    struct hdmr_block_pos double_indirect_block_pos = { 0, 0 };
    struct hdmr_datablock* double_indirect_block = kmalloc(4096, GFP_KERNEL);

    if (double_indirect_block == nullptr)
        r_info("zonefs: (error in hdmr_sub_lookup) cannot allocate 4KB heap space for datablock variable\n");

    double_indirect_block_pos = parent_hdmr_inode->double_indirect;
    double_indirect_block; //TODO : 저장장치의 double_indirect_block_pos으로부터 4KB의 구간을 double_indirect_block으로 읽어오기

    uint64_t result = find_inode_number_in_double_indirect_block(target_name, double_indirect_block, single_indirect_block, direct_block);

    if (result != 0) {
        kfree(double_indirect_block);
        kfree(single_indirect_block);
        kfree(direct_block);
        return result;
    }

    //triple indirect data block에서 특정 이름의 dentry 찾아보기
    struct hdmr_block_pos triple_indirect_block_pos = { 0, 0 };
    struct hdmr_datablock* triple_indirect_block = kmalloc(4096, GFP_KERNEL);

    if (triple_indirect_block == nullptr)
        r_info("zonefs: (error in hdmr_sub_lookup) cannot allocate 4KB heap space for datablock variable\n");

    triple_indirect_block_pos = parent_hdmr_inode->triple_indirect;
    triple_indirect_block; //TODO : 저장장치의 triple_indirect_block_pos으로부터 4KB의 구간을 triple_indirect_block으로 읽어오기

    uint64_t result = find_inode_number_in_triple_indirect_block(target_name, triple_indirect_block, double_indirect_block, single_indirect_block, direct_block);

    if (result != 0) {
        kfree(triple_indirect_block);
        kfree(double_indirect_block);
        kfree(single_indirect_block);
        kfree(direct_block);
        return result;
    }


    //전부를 뒤져보고도 못 찾았은 경우
    kfree(triple_indirect_block);
    kfree(double_indirect_block);
    kfree(single_indirect_block);
    kfree(direct_block);
    return 0;
}

uint64_t find_inode_number_in_direct_block(
    const char* target_name,
    hdmr_datablock* direct_block
) {
    for (int j = 4; j < 4096; j += sizeof(struct hdmr_dentry)) {
        struct hdmr_dentry temp_dentry;
        memcpy(&temp_dentry, direct_block + j, sizeof(struct hdmr_dentry));

        if (memcmp(temp_dentry.name, name, HDMR_MAX_NAME_LEN))
            return temp_dentry.i_ino;
    }

    return 0;
}

uint64_t find_inode_number_in_single_indirect_block(
    const char* target_name,
    hdmr_datablock* single_indirect_block,
    hdmr_datablock* temp_direct_block
) {
    for (int j = 4; j < 4096; j += sizeof(struct hdmr_block_pos)) {
        struct hdmr_block_pos temp_direct_block_pos;
        memcpy(&temp_direct_block_pos, single_indirect_block + j, sizeof(struct hdmr_block_pos));

        temp_direct_block; //TODO : 저장장치의 temp_direct_block_pos으로부터 4KB의 구간을 temp_direct_block으로 읽어오기

        uint64_t result = find_inode_number_in_direct_block(target_name, temp_direct_block);

        if (result != 0)
            return result;
    }

    return 0;
}

uint64_t find_inode_number_in_double_indirect_block(
    const char* target_name,
    hdmr_datablock* double_indirect_block,
    hdmr_datablock* temp_single_indirect_block,
    hdmr_datablock* temp_direct_block
) {
    for (int j = 4; j < 4096; j += sizeof(struct hdmr_block_pos)) {
        struct hdmr_block_pos temp_single_indirect_block_pos;
        memcpy(&temp_single_indirect_block_pos, double_indirect_block + j, sizeof(struct hdmr_block_pos));

        temp_single_indirect_block; //TODO : 저장장치의 temp_single_indirect_block_pos으로부터 4KB의 구간을 temp_single_indirect_block으로 읽어오기

        uint64_t result = find_inode_number_in_single_indirect_block(target_name, temp_single_indirect_block, temp_direct_block);

        if (result != 0)
            return result;
    }

    return 0;
}

uint64_t find_inode_number_in_triple_indirect_block(
    const char* target_name,
    hdmr_datablock* triple_indirect_block,
    hdmr_datablock* temp_double_indirect_block,
    hdmr_datablock* temp_single_indirect_block,
    hdmr_datablock* temp_direct_block
) {
    for (int j = 4; j < 4096; j += sizeof(struct hdmr_block_pos)) {
        struct hdmr_block_pos temp_double_indirect_block_pos;
        memcpy(&temp_double_indirect_block_pos, triple_indirect_block + j, sizeof(struct hdmr_block_pos));

        temp_double_indirect_block; //TODO : 저장장치의 temp_double_indirect_block_pos으로부터 4KB의 구간을 temp_double_indirect_block으로 읽어오기

        uint64_t result = find_inode_number_in_double_indirect_block(target_name, temp_double_indirect_block, temp_single_indirect_block, temp_direct_block);

        if (result != 0)
            return result;
    }

    return 0;
}