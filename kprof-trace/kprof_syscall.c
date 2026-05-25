// SPDX-License-Identifier: GPL-2.0
/*
 * kprof_syscall.c — Syscall tracer using tracepoints
 *
 * Hooks into sys_enter and sys_exit tracepoints to count syscalls
 * and measure their execution time for the target PID.
 *
 * We use raw tracepoints (tracepoint probes) which are the recommended
 * approach for tracing syscalls in modern kernels (>= 4.17).
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/tracepoint.h>
#include <linux/sched.h>
#include <linux/ktime.h>
#include <linux/slab.h>
#include <linux/hashtable.h>
#include <trace/events/syscalls.h>

#include "kprof.h"

/*
 * Per-task entry timestamp storage.
 *
 * When a syscall enters, we record ktime_get_ns() so we can compute
 * the duration at exit. We use a small hash table keyed by task PID
 * to store the entry timestamp and syscall number.
 */
struct kprof_entry_info {
	struct hlist_node   node;
	pid_t               pid;
	int                 syscall_nr;
	u64                 entry_ns;
};

#define KPROF_ENTRY_HASH_BITS  8
static DEFINE_HASHTABLE(entry_hash, KPROF_ENTRY_HASH_BITS);
static DEFINE_SPINLOCK(entry_hash_lock);

/* Tracepoint pointers (resolved at init) */
static struct tracepoint *tp_sys_enter;
static struct tracepoint *tp_sys_exit;
static bool tracepoints_registered;

/*
 * Find or create an entry_info for the current task.
 */
static struct kprof_entry_info *find_entry(pid_t pid)
{
	struct kprof_entry_info *info;

	hash_for_each_possible(entry_hash, info, node, pid) {
		if (info->pid == pid)
			return info;
	}
	return NULL;
}

/*
 * sys_enter tracepoint callback.
 *
 * Called when any syscall is entered. We check if the current task
 * matches our target PID and record the entry timestamp.
 */
static void kprof_sys_enter(void *data, struct pt_regs *regs, long id)
{
	struct kprof_entry_info *info;
	unsigned long flags;

	if (!kprof_should_trace())
		return;

	if (id < 0 || id >= KPROF_MAX_SYSCALLS)
		return;

	spin_lock_irqsave(&entry_hash_lock, flags);

	info = find_entry(current->pid);
	if (!info) {
		info = kmalloc(sizeof(*info), GFP_ATOMIC);
		if (!info) {
			spin_unlock_irqrestore(&entry_hash_lock, flags);
			return;
		}
		info->pid = current->pid;
		hash_add(entry_hash, &info->node, current->pid);
	}

	info->syscall_nr = (int)id;
	info->entry_ns = ktime_get_ns();

	spin_unlock_irqrestore(&entry_hash_lock, flags);
}

/*
 * sys_exit tracepoint callback.
 *
 * Called when any syscall exits. We compute the duration and update
 * the per-syscall statistics.
 */
static void kprof_sys_exit(void *data, struct pt_regs *regs, long ret)
{
	struct kprof_entry_info *info;
	unsigned long flags;
	u64 exit_ns, duration;
	int nr;

	if (!kprof_should_trace())
		return;

	exit_ns = ktime_get_ns();

	spin_lock_irqsave(&entry_hash_lock, flags);

	info = find_entry(current->pid);
	if (!info || info->entry_ns == 0) {
		spin_unlock_irqrestore(&entry_hash_lock, flags);
		return;
	}

	nr = info->syscall_nr;
	duration = exit_ns - info->entry_ns;
	info->entry_ns = 0; /* mark as consumed */

	spin_unlock_irqrestore(&entry_hash_lock, flags);

	if (nr >= 0 && nr < KPROF_MAX_SYSCALLS) {
		atomic64_inc(&kprof_state.syscalls[nr].count);
		atomic64_add(duration, &kprof_state.syscalls[nr].total_ns);
	}
}

/*
 * Tracepoint lookup callback — used by for_each_kernel_tracepoint()
 * to find sys_enter and sys_exit tracepoints.
 */
static void lookup_tracepoints(struct tracepoint *tp, void *priv)
{
	if (!strcmp(tp->name, "sys_enter"))
		tp_sys_enter = tp;
	else if (!strcmp(tp->name, "sys_exit"))
		tp_sys_exit = tp;
}

/*
 * Initialize syscall tracer.
 */
int kprof_syscall_init(void)
{
	int ret;

	tp_sys_enter = NULL;
	tp_sys_exit = NULL;
	tracepoints_registered = false;

	/* Find tracepoints */
	for_each_kernel_tracepoint(lookup_tracepoints, NULL);

	if (!tp_sys_enter || !tp_sys_exit) {
		pr_err(KPROF_NAME ": sys_enter/sys_exit tracepoints not found\n");
		return -ENOENT;
	}

	/* Register sys_enter probe */
	ret = tracepoint_probe_register(tp_sys_enter, kprof_sys_enter, NULL);
	if (ret) {
		pr_err(KPROF_NAME ": failed to register sys_enter probe (err=%d)\n", ret);
		return ret;
	}

	/* Register sys_exit probe */
	ret = tracepoint_probe_register(tp_sys_exit, kprof_sys_exit, NULL);
	if (ret) {
		pr_err(KPROF_NAME ": failed to register sys_exit probe (err=%d)\n", ret);
		tracepoint_probe_unregister(tp_sys_enter, kprof_sys_enter, NULL);
		return ret;
	}

	tracepoints_registered = true;
	pr_info(KPROF_NAME ": syscall tracer initialized\n");
	return 0;
}

/*
 * Cleanup syscall tracer.
 */
void kprof_syscall_exit(void)
{
	struct kprof_entry_info *info;
	struct hlist_node *tmp;
	unsigned long flags;
	int bkt;

	if (tracepoints_registered) {
		tracepoint_probe_unregister(tp_sys_enter, kprof_sys_enter, NULL);
		tracepoint_probe_unregister(tp_sys_exit, kprof_sys_exit, NULL);
		tracepoint_synchronize_unregister();
		tracepoints_registered = false;
	}

	/* Free all entry_info nodes */
	spin_lock_irqsave(&entry_hash_lock, flags);
	hash_for_each_safe(entry_hash, bkt, tmp, info, node) {
		hash_del(&info->node);
		kfree(info);
	}
	spin_unlock_irqrestore(&entry_hash_lock, flags);

	pr_info(KPROF_NAME ": syscall tracer stopped\n");
}
