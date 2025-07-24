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
    uint8_t  type; // 0: file, 1: directory

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

struct hdmr_datablock {
    char magic[4];
    char data[4092];
};

struct hdmr_mapping_info {
    struct hdmr_block_pos mapping_table[HDMR_MAX_INODE];
    int starting_ino;
    struct hdmr_block_pos wp;
    uint32_t bitmap[HDMR_MAX_INODE / 32];
};

struct hdmr_mapping_info mapping_info;

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

static int hdmr_create(struct mnt_idmap *idmap, struct inode *dir,struct dentry *dentry, umode_t mode, bool excl) {
    ZONEFS_TRACE();

    struct inode *inode;
    struct timespec64 now;
    struct hdmr_inode hinode;

    inode = new_inode(dir->i_sb);
    now = current_time(inode);  // inode의 superblock의 시간 정책에 따른 현재 시각

    // 여기부터 hinode 초기화: 함수로 리팩터링
    hinode.magic[0] = 'I';
    hinode.magic[1] = 'N';
    hinode.magic[2] = 'O';
    hinode.magic[3] = 'D';

    hinode.file_len = 0;

    hinode.name_len = dentry->d_name.len; 
    if (name_len > HDMR_MAX_NAME_LEN) {
        // 구현해야 될 부분: error handling
    }
    memcpy(hinode.name, dentry->d_name.name, hinode.name_len);

    hinode.type = 0;

    // 구현해야 될 부분: hdmr_get_next_ino();
    hinode.i_ino = hdmr_get_next_ino();
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
    inode->i_op   = &hdmr_inode_operations;
    inode->i_fop  = &hdmr_file_operations;
    inode->i_mode = S_IFREG | mode;
    inode->i_uid  = current_fsuid();
    inode->i_gid  = current_fsgid();

    inode_set_ctime_to_ts(inode, now);
    inode_set_atime_to_ts(inode, now);
    inode_set_mtime_to_ts(inode, now);

    d_add(dentry, inode);

    // 구현할 부분: zns-ssd에 hdmr inode를 저장하는 부분을 구현해야 됨.
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