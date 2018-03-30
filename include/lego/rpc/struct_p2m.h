/*
 * Copyright (c) 2016-2018 Wuklab, Purdue University. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _LEGO_RPC_STRUCT_P2M_H
#define _LEGO_RPC_STRUCT_P2M_H

#include <processor/pcache_config.h>
#include <lego/rpc/struct_common.h>

/*
 * P2M_MISS
 */

struct p2m_pcache_miss_struct {
	__u32	pid;
	__u32	tgid;
	__u32	flags;
	__u64	missing_vaddr;
};

#define PCACHE_MAPPING_ANON	0x1
#define PCACHE_MAPPING_FILE	0x2

struct p2m_pcache_miss_reply_struct {
	__u32	mapping_flags;
	__wsum	csum;
	char	data[PCACHE_LINE_SIZE];
};

int handle_p2m_pcache_miss(struct p2m_pcache_miss_struct *, u64,
			struct common_header *);

/* P2M_PCACHE_FLUSH */
struct p2m_flush_payload {
	u32		pid;
	unsigned long	user_va;
	char		pcacheline[PCACHE_LINE_SIZE];
};
int handle_p2m_flush_one(struct p2m_flush_payload *, u64, struct common_header *);

struct p2m_replica_msg {
	struct common_header	header;
	unsigned int		pid;
	unsigned long		user_va;
	char			pcacheline[PCACHE_LINE_SIZE];
};
void handle_p2m_replica(void *, u64);

/*
 * P2M_READ
 * P2M_WRITE
 */

/*
 * We need pass the filename, uid, flags, len, offset
 * and virtual address of user buffer to memory component
 * Also we need nid and pid to convert user virtual address
 * to coresponding kernel virtual address.
 */
struct p2m_read_write_payload {
	u32	pid;
	u32	tgid;
	char __user *buf;
	int	uid;
	char	filename[MAX_FILENAME_LENGTH];
	int	flags;
	ssize_t	len;
	loff_t	offset;
};
int handle_p2m_read(struct p2m_read_write_payload*, u64, struct common_header *);
int handle_p2m_write(struct p2m_read_write_payload*, u64, struct common_header *);

/*
 * P2M_CLOSE
 */
struct p2m_close_struct {
	__u32 pid;
};
int handle_p2m_close(struct p2m_close_struct *, u64, struct common_header *);

/*
 * P2M_FORK
 */
/* Task command name length */
#define LEGO_TASK_COMM_LEN 16

struct p_vm_area_struct {
	__u64	vm_start;
	__u64	vm_end;
	__u64	vm_flags;
};

struct p2m_fork_struct {
	__u32	pid;
	__u32	tgid;
	__u32	parent_tgid;
	__u32	clone_flags;
	char	comm[LEGO_TASK_COMM_LEN];
};
struct task_struct;
int p2m_fork(struct task_struct *p, unsigned long clone_flags);
int handle_p2m_fork(struct p2m_fork_struct *, u64, struct common_header *);

/*
 * P2M_EXECVE
 */
/*
 * These are the maximum length and maximum number of strings passed to the
 * execve() system call.  MAX_ARG_STRLEN is essentially random but serves to
 * prevent the kernel from being unduly impacted by misaddressed pointers.
 * MAX_ARG_STRINGS is chosen to fit in a signed 32-bit integer.
 */
#define MAX_ARG_STRLEN		(PAGE_SIZE * 32)
#define MAX_ARG_STRINGS		0x7FFFFFFF

struct p2m_execve_struct {
	__u32	pid;
	__u32	payload_size;
	char	filename[MAX_FILENAME_LENGTH];
	__u32	argc;
	__u32	envc;
	char	*array;
	/*
	 * NOTE:
	 * variable size struct
	 * the @payload_size means the total size
	 */
};
struct m2p_execve_struct {
	__u32	status;
	__u64	new_ip;
	__u64	new_sp;
#ifdef CONFIG_DISTRIBUTED_VMA
	struct vmr_map_reply map;
#endif
};
int handle_p2m_execve(struct p2m_execve_struct *, u64, struct common_header *);

/*
 * P2M_MMAP
 */
struct p2m_mmap_struct {
	__u32	pid;
	__u64	addr;
	__u64	len;
	__u64	prot;
	__u64	flags;
	__u64	pgoff;
	char	f_name[MAX_FILENAME_LENGTH];
};
struct p2m_mmap_reply_struct {
	__u32	ret;
	__u64	ret_addr;
#ifdef CONFIG_DISTRIBUTED_VMA
	struct vmr_map_reply map;
#endif
};
int handle_p2m_mmap(struct p2m_mmap_struct *, u64, struct common_header *);

/*
 * P2M_MUNMAP
 */
struct p2m_munmap_struct {
	__u32	pid;
	__u64	addr;
	__u64	len;
};
int handle_p2m_munmap(struct p2m_munmap_struct *, u64, struct common_header *);

/*
 * P2M_MREMAP
 */
struct p2m_mremap_struct {
	__u32	pid;
	__u64	old_addr;
	__u64	old_len;
	__u64	new_len;
	__u64	flags;
	__u64	new_addr;
};
struct p2m_mremap_reply_struct {
	__u32	status;
	__u32	line;			/* which line fails... */
	__u64	new_addr;
#ifdef CONFIG_DISTRIBUTED_VMA
	struct vmr_map_reply map;
#endif
};
int handle_p2m_mremap(struct p2m_mremap_struct *, u64, struct common_header *);

/*
 * P2M_MPROTECT
 */
struct p2m_mprotect_struct {
	__u32	pid;
	__u64	addr;
	__u64	len;
	__u32	prot;
};
int handle_p2m_mprotect(struct p2m_mprotect_struct *, u64, struct common_header *);

/*
 * P2M_BRK
 */
struct p2m_brk_struct {
	__u32	pid;
	__u64	brk;
};
struct p2m_brk_reply_struct {
	__u64 	ret_brk;
#ifdef CONFIG_DISTRIBUTED_VMA
	struct vmr_map_reply map;
#endif
};
int handle_p2m_brk(struct p2m_brk_struct *, u64, struct common_header *);

/*
 * P2M_MSYNC
 */
#define MS_ASYNC	1		/* sync memory asynchronously */
#define MS_INVALIDATE	2		/* invalidate the caches */
#define MS_SYNC		4		/* synchronous memory sync */
struct p2m_msync_struct {
	__u32	pid;
	__u64	start;
	__u64	len;
	__u32	flags;
};
int handle_p2m_msync(struct p2m_msync_struct *, u64, struct common_header *);

/*
 * P2M_CHECKPOINT
 */
int handle_p2m_checkpint(void *, u64, struct common_header *);

#endif /* _LEGO_RPC_STRUCT_P2M_H */
