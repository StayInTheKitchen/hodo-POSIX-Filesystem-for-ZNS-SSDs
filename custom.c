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
};




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


// inode operations

static int hdmr_setattr(struct mnt_idmap *idmap, struct dentry *dentry, struct iattr *iattr) {
    ZONEFS_TRACE();
    return zonefs_dir_inode_operations.setattr(idmap, dentry, iattr);
}

static struct dentry *hdmr_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags) {
    ZONEFS_TRACE();
    return zonefs_dir_inode_operations.lookup(dir, dentry, flags);
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

const struct address_space_operations custom_zonefs_file_aops = {
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