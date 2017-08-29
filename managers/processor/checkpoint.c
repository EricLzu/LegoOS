/*
 * Copyright (c) 2016-2017 Wuklab, Purdue University. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#define pr_fmt(fmt) "Checkpoint: " fmt

#include <lego/pid.h>
#include <lego/timer.h>
#include <lego/ktime.h>
#include <lego/sched.h>
#include <lego/kernel.h>
#include <lego/jiffies.h>
#include <lego/syscalls.h>
#include <lego/spinlock.h>
#include <lego/timekeeping.h>

#undef debug
#define debug(fmt,...) pr_info(fmt, ##__VA_ARGS__)

/* Timeout for waiting all threads reach to barrier */
unsigned long __read_mostly checkpoint_barrier_timeout_msec = 100;

/* Timeout for the real work of checkpointing to remote */
unsigned long __read_mostly checkpoint_job_timeout_msec = 1000*10;

#ifdef CONFIG_CHECKPOINT_DEBUG
static void paranoid_state_check(struct task_struct *leader)
{
	struct task_struct *t;
	unsigned long flags;

	/*
	 * Still need lock here in case there
	 * are someone skipping underneath. After all,
	 * you are very paranoid if you reach here:
	 */
	spin_lock_irqsave(&tasklist_lock, flags);
	for_each_thread(leader, t) {
		/* group leader itself is running this */
		if (leader == t)
			continue;
		if (unlikely(t->state != TASK_CHECKPOINTING))
			pr_info("BUG: t->state: %ld, t->pid: %d\n",
				t->state, t->pid);
	}
	spin_unlock_irqrestore(&tasklist_lock, flags);
}
#else
static void paranoid_state_check(struct task_struct *leader) { }
#endif

/*
 * Do the real work of checkpoint a whole thread-group
 * @p: thread group leader
 */
static void do_checkpoint_process(struct task_struct *leader)
{
	paranoid_state_check(leader);
}

static void wake_up_thread_group(struct task_struct *leader)
{
	struct task_struct *t;
	unsigned long flags;

	spin_lock_irqsave(&tasklist_lock, flags);
	for_each_thread(leader, t) {
		/* group leader itself is running this */
		if (leader == t)
			continue;
		if (!wake_up_state(t, TASK_CHECKPOINTING))
			WARN(1, "Fail to wake: %d-%d-state:%ld\n",
				t->pid, t->tgid, t->state);
	}
	spin_unlock_irqrestore(&tasklist_lock, flags);
}

static void barrier_timeout_wakeup(struct task_struct *leader)
{
	struct task_struct *t;
	unsigned long flags;
	int i = 0;

	debug("Abort due to barrier timeout. Leader-PID: %d, nr_threads: %d "
		"barrier_timeout_msec: %lu\n", leader->pid, leader->signal->nr_threads,
		checkpoint_barrier_timeout_msec);

	spin_lock_irqsave(&tasklist_lock, flags);
	for_each_thread(leader, t) {
		debug("    Thread %d: pid=%d, state=%ld, TIF_NEED_CHECKPOINT: %d\n",
			i++, t->pid, t->state, test_tsk_need_checkpoint(t));

		wake_up_state(t, TASK_ALL);
	}
	spin_unlock_irqrestore(&tasklist_lock, flags);
}

int checkpoint_thread(struct task_struct *p)
{
	struct task_struct *leader;
	long saved_state = p->state;

	debug("%s(): tsk: %d-%d\n", FUNC, p->pid, p->tgid);
	BUG_ON(!test_tsk_need_checkpoint(p));

	leader = p->group_leader;
	atomic_inc(&leader->process_barrier);

	if (p != leader) {
		set_current_state(TASK_CHECKPOINTING);
		schedule();

		/* Restore saved task state before returning: */
		set_current_state(saved_state);
	} else {
		ktime_t start, end, elapsed;
		unsigned long timeout, elapsed_msecs;

		start = ktime_get_boottime();
		timeout = jiffies + msecs_to_jiffies(checkpoint_barrier_timeout_msec);

		while (atomic_read(&p->process_barrier) != p->signal->nr_threads) {
			/*
			 * Abort whole checkpointing, and
			 * wake all threads:
			 */
			if (time_after(jiffies, timeout)) {
				barrier_timeout_wakeup(p);
				goto timeout;
			}
		}

		end = ktime_get_boottime();
		elapsed = ktime_sub(end, start);
		elapsed_msecs = ktime_to_ms(elapsed);
		debug("Barrier elapsed %lu.%3lu seconds\n",
			elapsed_msecs / 1000, elapsed_msecs % 1000);

		do_checkpoint_process(p);
		wake_up_thread_group(p);
	}

timeout:
	clear_tsk_thread_flag(p, TIF_NEED_CHECKPOINT);
	return 0;
}

/**
 * Checkpoint a thread group that @p belongs to.
 * This function is lightweight: set NEED_CHECKPOINT, kick all
 * threads to run, that is all. The real dirty work is done by
 * do_checkpoint_process() above.
 */
static int checkpoint_process(struct task_struct *p)
{
	struct task_struct *t;
	unsigned long flags;

	spin_lock_irqsave(&tasklist_lock, flags);
	for_each_thread(p, t) {
		debug("Set NEED_CHECKPOINT for tsk: %d-%d\n", t->pid, t->tgid);
		set_tsk_thread_flag(t, TIF_NEED_CHECKPOINT);

		if (!wake_up_state(t, TASK_ALL))
			kick_process(t);
	}
	spin_unlock_irqrestore(&tasklist_lock, flags);

	return 0;
}

SYSCALL_DEFINE1(checkpoint_process, pid_t, pid)
{
	struct task_struct *tsk;
	long ret = 0;

	syscall_enter("pid: %d\n", pid);

	tsk = find_task_by_pid(pid);
	if (!tsk) {
		ret = -ESRCH;
		goto out;
	}

	ret = checkpoint_process(tsk);
out:
	syscall_exit(ret);
	return ret;
}
