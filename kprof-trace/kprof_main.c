// SPDX-License-Identifier: GPL-2.0
/*
 * kprof_main.c — Main module entry point for kprof
 *
 * Initializes and tears down all subsystems:
 *   - Syscall tracer (tracepoints)
 *   - Page fault tracer (tracepoints)
 *   - Process lifecycle tracker (sched_process_exit)
 *   - Procfs interface (/proc/kprof/*)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include "kprof.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("kprof team");
MODULE_DESCRIPTION("Educational Linux Profiler — syscall & pagefault tracer");
MODULE_VERSION("0.1");

/* Global state instance */
struct kprof_state kprof_state;

/*
 * Module parameter: optionally set target PID at load time
 *   sudo insmod kprof.ko target_pid=1234
 */
static int target_pid_param = 0;
module_param_named(target_pid, target_pid_param, int, 0644);
MODULE_PARM_DESC(target_pid, "PID to trace (0 = disabled, set via /proc/kprof/control)");

static int __init kprof_init(void)
{
	int ret;

	pr_info(KPROF_NAME ": initializing...\n");

	/* Initialize global state */
	memset(&kprof_state, 0, sizeof(kprof_state));
	spin_lock_init(&kprof_state.state_lock);
	kprof_state.target_pid = target_pid_param;
	kprof_state.exclude_pid = 0;
	kprof_state.active = (target_pid_param != 0);
	kprof_state.target_alive = (target_pid_param != 0);

	/* Initialize procfs first (other subsystems may need it) */
	ret = kprof_proc_init();
	if (ret) {
		pr_err(KPROF_NAME ": failed to init procfs (err=%d)\n", ret);
		goto fail_proc;
	}

	/* Initialize syscall tracer */
	ret = kprof_syscall_init();
	if (ret) {
		pr_err(KPROF_NAME ": failed to init syscall tracer (err=%d)\n", ret);
		goto fail_syscall;
	}

	/* Initialize page fault tracer */
	ret = kprof_pagefault_init();
	if (ret) {
		pr_err(KPROF_NAME ": failed to init pagefault tracer (err=%d)\n", ret);
		goto fail_pagefault;
	}

	/* Initialize process lifecycle tracker */
	ret = kprof_lifecycle_init();
	if (ret) {
		pr_err(KPROF_NAME ": failed to init lifecycle tracker (err=%d)\n", ret);
		goto fail_lifecycle;
	}

	pr_info(KPROF_NAME ": loaded successfully (target_pid=%d, active=%s)\n",
		kprof_state.target_pid,
		kprof_state.active ? "yes" : "no");

	return 0;

fail_lifecycle:
	kprof_pagefault_exit();
fail_pagefault:
	kprof_syscall_exit();
fail_syscall:
	kprof_proc_exit();
fail_proc:
	return ret;
}

static void __exit kprof_exit(void)
{
	pr_info(KPROF_NAME ": unloading...\n");

	kprof_lifecycle_exit();
	kprof_pagefault_exit();
	kprof_syscall_exit();
	kprof_proc_exit();

	pr_info(KPROF_NAME ": unloaded\n");
}

module_init(kprof_init);
module_exit(kprof_exit);
