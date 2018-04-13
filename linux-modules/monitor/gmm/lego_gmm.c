/*
 * Copyright (c) 2016-2017 Wuklab, Purdue University. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/jiffies.h>

#include <common.h>
#include <gmm.h>

static unsigned long period;
static LIST_HEAD(mnodes);
static struct task_struct *status_polling;

static void handle_timer_request(unsigned long unused);
DEFINE_TIMER(timer, handle_timer_request, 0, 0);

int handle_m2mm_consult(struct consult_info *payload, u64 desc, struct common_header *hdr)
{
	unsigned long len = payload->len;
	int nid = choose_homenode();
	int ret = 0;
	struct consult_reply reply;

	pr_info("New memory request, length: %lx, memory chosen: %d\n", len, nid);
	reply.count = 1;
	reply.scheme[0].nid = nid;
	reply.scheme[0].len = len;

#if USE_IBAPI
	ret = ibapi_reply_message(&reply, sizeof(reply), desc);
#endif
	return ret;
}
EXPORT_SYMBOL(handle_m2mm_consult);

static void handle_timer_request(unsigned long unused)
{
	wake_up_process(status_polling);
	mod_timer(&timer, jiffies + period);
}

static inline void prepare_memstatus_payload(struct common_header *hdr)
{
	hdr->opcode = M2MM_STATUS_REPORT;
	hdr->src_nid = LEGO_LOCAL_NID;
	hdr->length = sizeof(*hdr);
}

static int request_memory_nodes_status(void *unused)
{
	int ret = 0;
	struct mnode_struct *pos;
	struct common_header hdr;
	struct m2mm_mnode_status_reply reply;

	while (1) {
		pr_info("Request New Status\n");

		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
		if (kthread_should_stop())
			break;

		prepare_memstatus_payload(&hdr);
		list_for_each_entry(pos, &mnodes, list) {
#if USE_IBAPI
			ret = ibapi_send_reply_imm(pos->nid, &hdr, sizeof(hdr), 
						   &reply, sizeof(reply), 0);
#endif
			if (ret < 0) {
				pr_info("couldn't retrieve memory information from node %d\n",
					pos->nid);
				continue;
			}
	
			pos->totalram = reply.totalram;
			pos->freeram = reply.freeram;
			pr_info("Node %d, new freeram: %lx, new totalram: %lx\n",
				pos->nid, pos->freeram, pos->totalram);
		}
	}
	return 0;
}

int choose_homenode(void)
{
#if ROUND_ROBIN_CHOOSE
	static int counter = 0;
	counter++;
	return mnode_nids[counter % MEMORY_NODE_COUNT];
#else
	struct mnode_struct *mnode;
	struct mnode_struct *target;
	target = mnode = list_first_entry(&mnodes, struct mnode_struct, list);

	list_for_each_entry(mnode, &mnode->list, list) {
		if (mnode->freeram > target->freeram)
			target = mnode;
	}
	return target ? target->nid : -1;
#endif
}
EXPORT_SYMBOL(choose_homenode);

static int lego_mnode_conn_setup(void)
{
	int i;
	struct mnode_struct *m;

	for (i = 0; i < MEMORY_NODE_COUNT; i++) {
		m = kmalloc(sizeof(struct mnode_struct), GFP_KERNEL);
		if (unlikely(!m))
			return -ENOMEM;

		m->nid = mnode_nids[i];
		m->totalram = 0;
		m->freeram = 0;
		list_add_tail(&m->list, &mnodes);
		pr_info("memory node with id %d is online\n", m->nid);
	}
	return 0;
}

static int __init lego_gmm_module_init(void)
{
	int ret;

	pr_info("lego memory monitor module init is called.\n");
	ret = lego_mnode_conn_setup();
	if (ret)
		return ret;
#if MNODES_STATUS_REQUEST
	period = msecs_to_jiffies(1000 * MNODES_STATUS_REQUEST_PERIOD);
	mod_timer(&timer, jiffies + period);

	status_polling = kthread_create(request_memory_nodes_status,
					NULL, "request_memory_nodes_status");
	return IS_ERR(status_polling);
#else
	return 0;
#endif
}

/*
 * Lego global memory monitor exit
 */
static void mnodes_free(void)
{
	struct mnode_struct *m, *n;
	list_for_each_entry_safe(m, n, &mnodes, list) {
		list_del(&m->list);
		kfree(m);
	}
}

static void __exit lego_gmm_module_exit(void)
{
	mnodes_free();
#if MNODES_STATUS_REQUEST
	del_timer(&timer);
	kthread_stop(status_polling);
#endif
	pr_info("lego memory monitor module exit\n");
}	

module_init(lego_gmm_module_init);
module_exit(lego_gmm_module_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Wuklab@Purdue");