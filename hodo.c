#include <linux/blkdev.h>
#include <linux/quotaops.h>

#include "zonefs.h"
#include "hodo.h"


// prototype: inode operations
static int hodo_setattr(struct mnt_idmap *idmap, struct dentry *dentry, struct iattr *iattr);
static struct dentry *hodo_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags);
static int hodo_create(struct mnt_idmap *idmap, struct inode *dir,struct dentry *dentry, umode_t mode, bool excl);
struct hodo_mapping_info mapping_info;

// init
void hodo_init() {
    ZONEFS_TRACE();
    mapping_info.mapping_table[0].zone_id = 1; 
}

// ---------- file_operations ----------
static int hodo_open(struct inode *inode, struct file *filp) {
    ZONEFS_TRACE();

    struct dentry *dentry = filp->f_path.dentry;
    const char *parent = dentry->d_parent->d_name.name;

    if (!strcmp(parent, "seq") || !strcmp(parent, "cnv")) {
        return zonefs_file_operations.open(inode, filp);
    }

    return 0;
}
static ssize_t hodo_read(struct file *file, char __user *buf, size_t count, loff_t *pos) {
    ZONEFS_TRACE();
    struct inode *inode = file_inode(file);

    if (S_ISDIR(inode->i_mode))
        return zonefs_dir_operations.read(file, buf, count, pos);

    return zonefs_file_operations.read(file, buf, count, pos);
}

static int hodo_release(struct inode *inode, struct file *filp) {
    ZONEFS_TRACE();

    struct dentry *dentry = filp->f_path.dentry;
    const char *parent = dentry->d_parent->d_name.name;

    if (!strcmp(parent, "seq") || !strcmp(parent, "cnv")) {
        return zonefs_file_operations.release(inode, filp);
    }

    return 0;
}

static int hodo_fsync(struct file *filp, loff_t start, loff_t end, int datasync) {
    ZONEFS_TRACE();
    return zonefs_file_operations.fsync(filp, start, end, datasync);
}

static int hodo_mmap(struct file *filp, struct vm_area_struct *vma) {
    ZONEFS_TRACE();
    return zonefs_file_operations.mmap(filp, vma);
}

static loff_t hodo_llseek(struct file *filp, loff_t offset, int whence) {
    ZONEFS_TRACE();
    struct inode *inode = file_inode(filp);

    if (S_ISDIR(inode->i_mode))
        return zonefs_dir_operations.llseek(filp, offset, whence);

    return zonefs_file_operations.llseek(filp, offset, whence);
}

static ssize_t hodo_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	ZONEFS_TRACE();
	return zonefs_file_operations.read_iter(iocb, to);
}


static ssize_t hodo_write_iter(struct kiocb *iocb, struct iov_iter *from) {
    ZONEFS_TRACE();
    return zonefs_file_operations.write_iter(iocb, from);
}

static ssize_t hodo_splice_read(struct file *in, loff_t *ppos,
                                              struct pipe_inode_info *pipe, size_t len, unsigned int flags) {
    ZONEFS_TRACE();
    return zonefs_file_operations.splice_read(in, ppos, pipe, len, flags);
}

static ssize_t hodo_splice_write(struct pipe_inode_info *pipe, struct file *out,
                                             loff_t *ppos, size_t len, unsigned int flags) {
    ZONEFS_TRACE();
    return zonefs_file_operations.splice_write(pipe, out, ppos, len, flags);
}

static int hodo_iocb_bio_iopoll(struct kiocb *iocb, struct io_comp_batch *iob, unsigned int flags) {
    ZONEFS_TRACE();
    return zonefs_file_operations.iopoll(iocb, iob, flags);
}

static int hodo_readdir(struct file *file, struct dir_context *ctx) {
    ZONEFS_TRACE();
    return zonefs_dir_operations.iterate_shared(file, ctx);
}

const struct file_operations hodo_file_operations = {
    .open           = hodo_open,
    .read           = hodo_read,
    .release        = hodo_release,
    .fsync          = hodo_fsync,
    .mmap           = hodo_mmap,
    .llseek         = hodo_llseek,
    .read_iter      = hodo_read_iter,
    .write_iter     = hodo_write_iter,
    .splice_read    = hodo_splice_read,
    .splice_write   = hodo_splice_write,
    .iopoll         = hodo_iocb_bio_iopoll,
    .iterate_shared = hodo_readdir,
};


// inode operations
const struct inode_operations hodo_inode_operations = {
    .lookup  = hodo_lookup,
    .setattr = hodo_setattr,
    .create = hodo_create,
};

static int hodo_setattr(struct mnt_idmap *idmap, struct dentry *dentry, struct iattr *iattr) {
    ZONEFS_TRACE();
    return zonefs_dir_inode_operations.setattr(idmap, dentry, iattr);
}

static struct dentry *hodo_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags) {
    ZONEFS_TRACE();
    return zonefs_dir_inode_operations.lookup(dir, dentry, flags);
}

static int hodo_get_next_ino(void) {
    return 1;
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
    inode->i_op   = &hodo_inode_operations;
    inode->i_fop  = &hodo_file_operations;
    inode->i_mode = S_IFREG | mode;
    inode->i_uid  = current_fsuid();
    inode->i_gid  = current_fsgid();

    inode_set_ctime_to_ts(inode, now);
    inode_set_atime_to_ts(inode, now);
    inode_set_mtime_to_ts(inode, now);

    d_add(dentry, inode);

    // 구현할 부분: zns-ssd에 hodo inode를 저장하는 부분을 구현해야 됨.
    return 0;
}



// ---------- aops ----------
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