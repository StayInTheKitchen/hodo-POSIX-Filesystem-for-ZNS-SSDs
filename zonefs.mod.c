#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/export-internal.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

#ifdef CONFIG_UNWINDER_ORC
#include <asm/orc_header.h>
ORC_HEADER;
#endif

BUILD_SALT;
BUILD_LTO_INFO;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif



static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0xcc93fdb1, "filp_open" },
	{ 0xd88b6c28, "inode_init_owner" },
	{ 0x7c00ab63, "iomap_readahead" },
	{ 0xc8dcc62a, "krealloc" },
	{ 0x1d9fa975, "vfs_fsync_range" },
	{ 0x4f169501, "setattr_prepare" },
	{ 0x46b9b5d3, "filemap_migrate_folio" },
	{ 0x9d41f886, "iget_locked" },
	{ 0x2dce670f, "__bio_add_page" },
	{ 0xda61d3e0, "iocb_bio_iopoll" },
	{ 0xb8a16f7d, "new_inode" },
	{ 0x656e4a6e, "snprintf" },
	{ 0xa6257a2f, "complete" },
	{ 0x948a1f8b, "filemap_map_pages" },
	{ 0x5db5673a, "iomap_file_buffered_write" },
	{ 0x73afd6a1, "trace_raw_output_prep" },
	{ 0xda1bb76, "unregister_filesystem" },
	{ 0x48d88a2c, "__SCT__preempt_schedule" },
	{ 0x608741b5, "__init_swait_queue_head" },
	{ 0x61b6af7, "generic_error_remove_folio" },
	{ 0xfadee720, "__trace_trigger_soft_disabled" },
	{ 0xf1de6e5f, "d_make_root" },
	{ 0xe428c419, "trace_event_printf" },
	{ 0x53569707, "this_cpu_off" },
	{ 0x4c236f6f, "__x86_indirect_thunk_r15" },
	{ 0xacda2e0, "sb_set_blocksize" },
	{ 0xa47bfc3b, "sync_filesystem" },
	{ 0x95ea6e70, "d_splice_alias" },
	{ 0xa2b51896, "current_time" },
	{ 0x562a2e6, "trace_event_raw_init" },
	{ 0x69acdf38, "memcpy" },
	{ 0x6fe0ba9f, "inode_dio_wait" },
	{ 0x37a0cba, "kfree" },
	{ 0x6c54c9d, "iput" },
	{ 0xb9491cdb, "pcpu_hot" },
	{ 0xe953b21f, "get_next_ino" },
	{ 0x989a7cb8, "iter_file_splice_write" },
	{ 0xdbe38f2e, "bpf_trace_run2" },
	{ 0xffc3b45b, "register_filesystem" },
	{ 0x925b8b85, "dquot_transfer" },
	{ 0xd51afcc7, "kmem_cache_create" },
	{ 0xbf48a7d7, "iomap_dirty_folio" },
	{ 0xba8fbd64, "_raw_spin_lock" },
	{ 0xc3ff38c2, "down_read_trylock" },
	{ 0xcbd4898c, "fortify_panic" },
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0xe783e261, "sysfs_emit" },
	{ 0x7d2898ff, "trace_event_buffer_commit" },
	{ 0x8b141fff, "simple_inode_init_ts" },
	{ 0xdedd03f4, "iomap_invalidate_folio" },
	{ 0x284faa6b, "__x86_indirect_thunk_r11" },
	{ 0x65487097, "__x86_indirect_thunk_rax" },
	{ 0xb9f93f9d, "iomap_dio_rw" },
	{ 0x5217d130, "kill_block_super" },
	{ 0x374097fc, "unlock_new_inode" },
	{ 0x122c3a7e, "_printk" },
	{ 0xe6b731ec, "blkdev_zone_mgmt" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0x8f8a32a4, "bdev_nr_zones" },
	{ 0x32fb7dea, "make_kuid" },
	{ 0x60f6ebef, "fs_kobj" },
	{ 0xd6cf2625, "__free_pages" },
	{ 0x87a21cb3, "__ubsan_handle_out_of_bounds" },
	{ 0xe0812065, "iomap_page_mkwrite" },
	{ 0x7cd8d75e, "page_offset_base" },
	{ 0x599fb41c, "kvmalloc_node" },
	{ 0x5077329d, "inode_init_once" },
	{ 0x273effc, "file_write_and_wait_range" },
	{ 0xad5f0017, "perf_trace_buf_alloc" },
	{ 0xc73babba, "perf_trace_run_bpf_submit" },
	{ 0x57bc19d2, "down_write" },
	{ 0xce807a25, "up_write" },
	{ 0x4c8c8221, "generic_file_read_iter" },
	{ 0x8df92f66, "memchr_inv" },
	{ 0x55385e2e, "__x86_indirect_thunk_r14" },
	{ 0x4c03a563, "random_kmalloc_seed" },
	{ 0xf6bdd9bf, "setattr_copy" },
	{ 0x69dd3b5b, "crc32_le" },
	{ 0xd7cf837, "file_update_time" },
	{ 0x4dfa8d4b, "mutex_lock" },
	{ 0xba9a62a8, "set_nlink" },
	{ 0x43cf1221, "kmem_cache_free" },
	{ 0xf6308b7, "iomap_swapfile_activate" },
	{ 0x72496369, "trace_event_reg" },
	{ 0x7d679d8c, "iov_iter_kvec" },
	{ 0xe5db82f, "kobject_init_and_add" },
	{ 0x5a5a2271, "__cpu_online_mask" },
	{ 0x449ad0a7, "memcmp" },
	{ 0xcefb0c9f, "__mutex_init" },
	{ 0x81a63681, "bio_init" },
	{ 0x25974000, "wait_for_completion" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0x794e9828, "make_kgid" },
	{ 0xe40c37ea, "down_write_trylock" },
	{ 0xf18d53d9, "kobject_create_and_add" },
	{ 0xe2d5255a, "strcmp" },
	{ 0x668b19a1, "down_read" },
	{ 0xf92e585f, "truncate_setsize" },
	{ 0xe2e7ace, "bpf_trace_run3" },
	{ 0x446b47e0, "iomap_read_folio" },
	{ 0x97651e6c, "vmemmap_base" },
	{ 0xa648e561, "__ubsan_handle_shift_out_of_bounds" },
	{ 0x50e0e624, "generic_read_dir" },
	{ 0x7ccd19a3, "kobject_del" },
	{ 0xb5210616, "trace_event_buffer_reserve" },
	{ 0xfb3facd1, "d_add" },
	{ 0x85df9b6c, "strsep" },
	{ 0x9508cfda, "mount_bdev" },
	{ 0x3213f038, "mutex_unlock" },
	{ 0xaeacdd0, "iomap_writepages" },
	{ 0x92a715e5, "alloc_pages" },
	{ 0x6cc2f31e, "filp_close" },
	{ 0xef2d1073, "inc_nlink" },
	{ 0x44e9a829, "match_token" },
	{ 0x56470118, "__warn_printk" },
	{ 0x6c82de8d, "touch_atime" },
	{ 0x5ce7b7e6, "blk_op_str" },
	{ 0x9d62c70e, "__percpu_down_read" },
	{ 0xcdef6213, "generic_file_open" },
	{ 0x594f6477, "blkdev_issue_flush" },
	{ 0x45c35212, "seq_puts" },
	{ 0xa1e9b962, "rcuwait_wake_up" },
	{ 0x9ad4df9d, "send_sig" },
	{ 0x3aebb452, "filemap_splice_read" },
	{ 0xe4df0660, "kmalloc_trace" },
	{ 0x60a13e90, "rcu_barrier" },
	{ 0x19695699, "generic_file_llseek_size" },
	{ 0x54b1fac6, "__ubsan_handle_load_invalid_value" },
	{ 0x754d539c, "strlen" },
	{ 0x7aa1756e, "kvfree" },
	{ 0x688e72e1, "__SCT__preempt_schedule_notrace" },
	{ 0xffd10b0d, "kmem_cache_alloc_lru" },
	{ 0x96c1161d, "filemap_fault" },
	{ 0xb5b54b34, "_raw_spin_unlock" },
	{ 0x47152b24, "generic_file_llseek" },
	{ 0x277ab25, "iomap_release_folio" },
	{ 0x81f588df, "iomap_is_partially_uptodate" },
	{ 0x85d120e3, "trace_handle_return" },
	{ 0x53b954a2, "up_read" },
	{ 0xac699bf3, "submit_bio_wait" },
	{ 0xda113484, "blkdev_report_zones" },
	{ 0xe2c17b5d, "__SCT__might_resched" },
	{ 0x99433119, "kmalloc_caches" },
	{ 0x651b8049, "kmem_cache_destroy" },
	{ 0x219950d3, "kobject_put" },
	{ 0xbc314156, "nop_mnt_idmap" },
	{ 0x5eea5f17, "module_layout" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "9CAF39451408492C4C59F69");
