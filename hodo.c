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
static int hodo_sub_setattr(struct mnt_idmap *idmap, struct dentry *dentry, struct iattr *iattr);
uint64_t find_inode_number(struct hodo_inode *parent_hodo_inode, const char *target_name);
uint64_t find_inode_number_from_direct_block(const char *target_name, struct hodo_datablock *direct_block);
uint64_t find_inode_number_from_indirect_block(const char *target_name,struct hodo_datablock *indirect_block);
int read_all_dirents(struct hodo_inode *dir_hodo_inode, struct file *file, struct dir_context *ctx, uint64_t *dirent_count);
int read_all_dirents_from_direct_block(struct hodo_datablock* direct_block, struct file *file, struct dir_context *ctx, uint64_t *dirent_count);
int read_all_dirents_from_indirect_block(struct hodo_datablock* indirect_block, struct file *file, struct dir_context *ctx, uint64_t *dirent_count);
bool is_dirent_valid(struct hodo_dirent *dirent);
ssize_t hodo_read_struct(struct hodo_block_pos block_pos, char *out_buf, size_t len);
ssize_t hodo_write_struct(char *buf, size_t len);
ssize_t hodo_read_on_disk_mapping_info(void);
static int hodo_get_next_ino(void);
int add_dirent(struct inode* dir, struct hodo_inode* sub_inode);

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
        mapping_info.mapping_table[root_inode.i_ino - mapping_info.starting_ino].zone_id = mapping_info.wp.zone_id; 
        mapping_info.mapping_table[root_inode.i_ino - mapping_info.starting_ino].offset = 0;
        hodo_write_struct((char*)&root_inode, sizeof(root_inode));


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

    struct inode *inode = file_inode(file);
    struct dentry *dentry = file->f_path.dentry;
    const char *name = dentry->d_name.name;

    //보다 쉬운 디버깅을 위해, 'cnv', 'seq' 디렉토리에서 친 ls 명령어의 경우에는 기존의 zonefs readdir을 호출하도록 한다.
    if (!strcmp(name, "seq") || !strcmp(name, "cnv")) {
        pr_info("zonefs: readdir on 'seq' or 'cnv'\n");
        return zonefs_dir_operations.iterate_shared(file, ctx);
    }
    pr_info("1111111111111111111\n");
    //ctx->pos는 이제 읽어야 하는 dirent('.', '..', 'hodo_dirent')의 0-based 인덱스를 나타낸다
    //inode->i_size/sizeof(struct hodo_dirent)는 hodo 아이노드에 저장된 hodo_dirent의 개수를 나타낸다
    //따라서 둘을 비교해서 이 함수가 수행될 필요가 없는 경우(책갈피가 마지막장을 넘은 경우)를 빠르게 알아낼 수 있다.
    // uint64_t total_of_dirent = inode->i_size/sizeof(struct hodo_dirent);
    // if(ctx->pos >= total_of_dirent)
    //     return 0;

    //앞으로 '.', '..', 'hodo_dirent'들을 차례로 읽어나간다.
    //readdir은 ctx->pos로 어디까지 읽었는지를 기록하고서 중간에 중단되고 다시 호출될 수 있기 때문에,
    //읽는 대로 다 emit해주면 안되고, 그 차례가 ctx->pos와 일치하는 것부터만 emit하고 ctx->pos++를 해줘야한다.
    uint64_t dirent_count = 0;

    pr_info("2222222222222222222\n");
    //ctx->pos가 0~1이면'.'과 '..'을 읽으려고 한다. 
    //둘 다 읽어 ctx->pos가 2로 설정되고 true를 받아 readdir의 다음 단계로 진행한다.
    //false를 받게 된다면 이 함수의 호출자로도 false를 반환하여 readdir이 더이상 수행될 수 없음을 보고한다.
    if (!dir_emit_dots(file, ctx))
        return 0;

    dirent_count = 2;
        
    //루트 디렉터리에서의 ls : 아이노드 번호 0번을 사용
    //그외 사용자 디렉토리에서의 ls : 그냥 주어진 아이노드 번호를 사용
    uint64_t dir_hodo_mapping_index = 0;

    pr_info("333333333333333333\n");
    if (inode == d_inode(inode->i_sb->s_root)){
        pr_info("zonefs: readdir on root mount point\n");
        dir_hodo_mapping_index = 0;
    }
    else {
        pr_info("zonefs: readdir on non-root user dir\n");
        dir_hodo_mapping_index = inode->i_ino - mapping_info.starting_ino;
    }

    //디렉토리의 hodo 아이노드를 저장장치로부터 읽어온다
    struct hodo_block_pos dir_hodo_inode_pos = mapping_info.mapping_table[dir_hodo_mapping_index];
    struct hodo_inode dir_hodo_inode = { 0, };
    pr_info("pos: zone_id:%d\toffset:%d\n", dir_hodo_inode_pos.zone_id, dir_hodo_inode_pos.offset);
    hodo_read_struct(dir_hodo_inode_pos, (char*)&dir_hodo_inode, sizeof(struct hodo_inode));
    pr_info("name: %s\n", dir_hodo_inode.name);

    //디렉토리 hodo 아이노드가 직간접적으로 가리키는 블럭 안의 덴트리들을 모조리 읽는다
    //모두 잘 읽었다면 더 읽을 필요가 없음을 보고하기 위해 false를 반환한다
    return read_all_dirents(&dir_hodo_inode, file, ctx, &dirent_count);
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

    const char *name = dentry->d_name.name;
    const char *parent = dentry->d_parent->d_name.name;

    if (!strcmp(name, "seq") || !strcmp(name, "cnv") ||
        !strcmp(parent, "seq") || !strcmp(parent, "cnv")) {
        pr_info("zonefs: using original setattr for '%s' (parent: %s)\n", name, parent);
        return zonefs_dir_inode_operations.setattr(idmap, dentry, iattr);
    }

    return hodo_sub_setattr(idmap, dentry, iattr);
}

static struct dentry *hodo_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags) {
    ZONEFS_TRACE();

    pr_info("zonefs : sizeof(struct hodo_datablock) = %d\n", sizeof(struct hodo_datablock));
    pr_info("zonefs : sizeof(struct hodo_inode) = %d\n", sizeof(struct hodo_inode));
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

static int hodo_create(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl) {
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

    hinode.type = HODO_TYPE_REG;

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

    mapping_info.mapping_table[hinode.i_ino - mapping_info.starting_ino].zone_id = mapping_info.wp.zone_id; 
    mapping_info.mapping_table[hinode.i_ino - mapping_info.starting_ino].offset = mapping_info.wp.offset;
    hodo_write_struct((char*)&hinode, sizeof(struct hodo_inode));

    add_dirent(dir, &hinode);

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
    struct hodo_block_pos parent_hodo_inode_pos = {0, 0};
    if(dir == dentry->d_sb->s_root->d_inode)    //루트 노드의 hodo 아이노드 상의 번호는 vfs 아이노드 상의 번호와 달리 0번이니 조작한다 
        parent_hodo_inode_pos = mapping_info.mapping_table[0];
    else
        parent_hodo_inode_pos = mapping_info.mapping_table[parent_hodo_inode_number - mapping_info.starting_ino];
    struct hodo_inode parent_hodo_inode = { 0, };
    hodo_read_struct(parent_hodo_inode_pos, (char*)&parent_hodo_inode, sizeof(struct hodo_inode));

    //찾고자 하는 이름을 가진 hodo 아이노드를 읽어온다
    uint64_t target_hodo_inode_number = find_inode_number(&parent_hodo_inode, name);
    if(target_hodo_inode_number == 0){          //해당 이름의 아이노드가 저장장치에 없다면, 그냥 없다고 보고하자
        d_add(dentry, NULL);
        return dentry;
    }

    pr_info("zonefs: target hodo inode number: %d\n", target_hodo_inode_number);
    struct hodo_block_pos target_hodo_inode_pos = mapping_info.mapping_table[target_hodo_inode_number - mapping_info.starting_ino];
    struct hodo_inode target_hodo_inode = { 0, };
    hodo_read_struct(target_hodo_inode_pos, (char*)&target_hodo_inode, sizeof(struct hodo_inode));

    //찾던 이름의 hodo 아이노드 정보를 통해 VFS 아이노드를 구성하자
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
        memcpy(&temp_dirent, (void*)direct_block + j, sizeof(struct hodo_dirent));
        
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

static int hodo_sub_setattr(struct mnt_idmap *idmap, struct dentry *dentry, struct iattr *iattr) {
    ZONEFS_TRACE();

	struct inode *inode = d_inode(dentry);
	int ret;

	if (unlikely(IS_IMMUTABLE(inode)))
		return -EPERM;

	ret = setattr_prepare(&nop_mnt_idmap, dentry, iattr);
    if (ret) {
    	if (d_inode(dentry) == dentry->d_sb->s_root->d_inode) {
	    	pr_info("zonefs: ignoring setattr_prepare failure on root\n");
	    } else {
		    return ret;
	    }
    }


	if (((iattr->ia_valid & ATTR_UID) &&
	     !uid_eq(iattr->ia_uid, inode->i_uid)) ||
	    ((iattr->ia_valid & ATTR_GID) &&
	     !gid_eq(iattr->ia_gid, inode->i_gid))) {
		ret = dquot_transfer(&nop_mnt_idmap, inode, iattr);
		if (ret)
			return ret;
	}

	if (iattr->ia_valid & ATTR_SIZE) {
		ret = zonefs_file_truncate(inode, iattr->ia_size);
		if (ret)
			return ret;
	}

	setattr_copy(&nop_mnt_idmap, inode, iattr);

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
                memcpy(&temp_dirent, (void*)temp_datablock + j, sizeof(struct hodo_dirent));
                
                if(is_dirent_valid(&temp_dirent)) {
                    continue; 
                }
                else {
                    memcpy(temp_dirent.name, sub_inode->name, sub_inode->name_len);
                    temp_dirent.name_len = sub_inode->name_len;
                    temp_dirent.i_ino = sub_inode->i_ino;
                    temp_dirent.file_type = sub_inode->type;

                    temp_datablock->magic[0] = 'D';
                    temp_datablock->magic[1] = 'A';
                    temp_datablock->magic[2] = 'T';
                    temp_datablock->magic[3] = '0';

                    memcpy((void*)temp_datablock + j, &temp_dirent, sizeof(struct hodo_dirent));

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

            memcpy((void*)temp_datablock + 4, &temp_dirent, sizeof(struct hodo_dirent));

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