/* SPDX-License-Identifier: GPL-2.0 */
/*
 * kprof.h — Shared structures and declarations for kprof kernel module
 *
 * Educational Linux Profiler: syscall tracing + page fault tracing + procfs
 */

#ifndef _KPROF_H
#define _KPROF_H

#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>

/* Maximum number of syscalls we track (x86_64 has ~450, we cap at 512) */
#define KPROF_MAX_SYSCALLS  512

/* Module name used in pr_* logging and procfs */
#define KPROF_NAME          "kprof"
#define KPROF_PROC_DIR      "kprof"

/*
 * Per-syscall statistics.
 * We use atomic operations for lock-free updates from kprobe handlers.
 */
struct kprof_syscall_stat {
	atomic64_t count;       /* number of invocations */
	atomic64_t total_ns;    /* cumulative time in nanoseconds */
};

/*
 * Page fault statistics.
 */
struct kprof_pagefault_stat {
	atomic64_t minor_faults;
	atomic64_t major_faults;
	atomic64_t last_address; /* last faulting address (informational) */
};

/*
 * Global kprof state — single instance, protected by state_lock for
 * configuration changes (start/stop/reset). Statistics use atomics.
 */
struct kprof_state {
	/* Configuration — protected by state_lock */
	spinlock_t              state_lock;
	pid_t                   target_pid;   /* PID to trace, 0 = disabled */
	bool                    active;       /* tracing enabled? */

	/* Statistics — lock-free via atomics */
	struct kprof_syscall_stat   syscalls[KPROF_MAX_SYSCALLS];
	struct kprof_pagefault_stat pagefaults;

	/* Timestamps for per-syscall timing (per-task, stored in kprobe ctx) */
	/* We use ktime_get_ns() at entry/exit */
};

/* Global state instance (defined in kprof_main.c) */
extern struct kprof_state kprof_state;

/* Procfs directory entry (defined in kprof_proc.c) */
extern struct proc_dir_entry *kprof_proc_dir;

/*
 * Syscall tracer — kprof_syscall.c
 */
int  kprof_syscall_init(void);
void kprof_syscall_exit(void);

/*
 * Page fault tracer — kprof_pagefault.c
 */
int  kprof_pagefault_init(void);
void kprof_pagefault_exit(void);

/*
 * Procfs interface — kprof_proc.c
 */
int  kprof_proc_init(void);
void kprof_proc_exit(void);

/*
 * Helper: reset all statistics to zero
 */
static inline void kprof_reset_stats(void)
{
	int i;

	for (i = 0; i < KPROF_MAX_SYSCALLS; i++) {
		atomic64_set(&kprof_state.syscalls[i].count, 0);
		atomic64_set(&kprof_state.syscalls[i].total_ns, 0);
	}
	atomic64_set(&kprof_state.pagefaults.minor_faults, 0);
	atomic64_set(&kprof_state.pagefaults.major_faults, 0);
	atomic64_set(&kprof_state.pagefaults.last_address, 0);
}

/*
 * Helper: check if current task should be traced
 */
static inline bool kprof_should_trace(void)
{
	if (!kprof_state.active)
		return false;
	if (kprof_state.target_pid == 0)
		return false;
	return current->pid == kprof_state.target_pid ||
	       current->tgid == kprof_state.target_pid;
}

#endif /* _KPROF_H */
