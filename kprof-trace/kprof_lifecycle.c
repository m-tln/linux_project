// SPDX-License-Identifier: GPL-2.0
/*
 * kprof_lifecycle.c — Process lifecycle tracker
 *
 * Hooks into the sched_process_exit tracepoint to detect when the
 * target PID dies. This prevents:
 *   1. Tracing a dead PID (wasted overhead)
 *   2. Tracing a recycled PID (wrong process)
 *
 * When the target process exits, we set kprof_state.target_alive = false
 * and log a message. The orchestrator can detect this via /proc/kprof/config.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/tracepoint.h>
#include <linux/sched.h>

#include "kprof.h"

static struct tracepoint *tp_sched_process_exit;
static bool lifecycle_registered;

/*
 * sched_process_exit tracepoint callback.
 *
 * Fires when any process exits. We check if it's our target PID
 * and deactivate tracing if so.
 */
static void kprof_process_exit_cb(void *data, struct task_struct *task)
{
	unsigned long flags;

	if (!kprof_state.active)
		return;

	if (task->pid != kprof_state.target_pid &&
	    task->tgid != kprof_state.target_pid)
		return;

	/*
	 * Target process (or thread group leader) is exiting.
	 * Mark as dead to prevent tracing a recycled PID.
	 */
	spin_lock_irqsave(&kprof_state.state_lock, flags);
	kprof_state.target_alive = false;
	spin_unlock_irqrestore(&kprof_state.state_lock, flags);

	pr_info(KPROF_NAME ": target process %d exited — tracing paused\n",
		task->pid);
}

/*
 * Tracepoint lookup callback.
 */
static void lookup_tracepoints(struct tracepoint *tp, void *priv)
{
	if (!strcmp(tp->name, "sched_process_exit"))
		tp_sched_process_exit = tp;
}

/*
 * Initialize lifecycle tracker.
 */
int kprof_lifecycle_init(void)
{
	int ret;

	tp_sched_process_exit = NULL;
	lifecycle_registered = false;

	for_each_kernel_tracepoint(lookup_tracepoints, NULL);

	if (!tp_sched_process_exit) {
		pr_warn(KPROF_NAME ": sched_process_exit tracepoint not found "
			"— PID lifecycle tracking disabled\n");
		return 0; /* non-fatal */
	}

	ret = tracepoint_probe_register(tp_sched_process_exit,
					kprof_process_exit_cb, NULL);
	if (ret) {
		pr_warn(KPROF_NAME ": failed to register sched_process_exit "
			"probe (err=%d)\n", ret);
		return 0; /* non-fatal */
	}

	lifecycle_registered = true;
	pr_info(KPROF_NAME ": process lifecycle tracker initialized\n");
	return 0;
}

/*
 * Cleanup lifecycle tracker.
 */
void kprof_lifecycle_exit(void)
{
	if (lifecycle_registered && tp_sched_process_exit) {
		tracepoint_probe_unregister(tp_sched_process_exit,
					    kprof_process_exit_cb, NULL);
		tracepoint_synchronize_unregister();
		lifecycle_registered = false;
	}

	pr_info(KPROF_NAME ": process lifecycle tracker stopped\n");
}
