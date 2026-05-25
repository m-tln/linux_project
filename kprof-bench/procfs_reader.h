/*
 * procfs_reader.h — API for reading /proc/kprof/* and /proc/[pid]/* data
 *
 * Parses procfs files to extract syscall counts, page fault stats,
 * memory usage (VmRSS, VmSize), and process maps.
 */

#ifndef PROCFS_READER_H
#define PROCFS_READER_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

/* Maximum syscalls we track (must match kernel module KPROF_MAX_SYSCALLS) */
#define KPROF_MAX_SYSCALLS  512

/*
 * Syscall statistics read from /proc/kprof/syscalls
 */
struct kprof_syscall_entry {
	int          nr;            /* syscall number */
	char         name[32];      /* syscall name */
	uint64_t     count;         /* invocation count */
	uint64_t     total_ns;      /* total time in nanoseconds */
	uint64_t     avg_ns;        /* average time per call */
};

struct kprof_syscall_data {
	struct kprof_syscall_entry entries[KPROF_MAX_SYSCALLS];
	int                        num_entries;  /* number of valid entries */
};

/*
 * Page fault statistics read from /proc/kprof/pagefaults
 */
struct kprof_pagefault_data {
	uint64_t minor_faults;
	uint64_t major_faults;
	uint64_t total_faults;
	uint64_t last_fault_addr;
};

/*
 * Process memory info read from /proc/[pid]/status
 */
struct proc_mem_info {
	long vm_size_kb;     /* VmSize: total virtual memory */
	long vm_rss_kb;      /* VmRSS: resident set size */
	long vm_peak_kb;     /* VmPeak: peak virtual memory */
	long vm_swap_kb;     /* VmSwap: swapped out memory */
	long vol_ctxt_sw;    /* voluntary_ctxt_switches */
	long invol_ctxt_sw;  /* nonvoluntary_ctxt_switches */
};

/*
 * Snapshot: captures all profiling data at a point in time.
 * Used for before/after comparison in benchmarks.
 */
struct kprof_snapshot {
	struct kprof_syscall_data   syscalls;
	struct kprof_pagefault_data pagefaults;
	struct proc_mem_info        mem;
	bool                        has_syscalls;
	bool                        has_pagefaults;
	bool                        has_mem;
};

/*
 * Read /proc/kprof/syscalls into structured data.
 * Returns 0 on success, -1 on error.
 */
int procfs_read_syscalls(struct kprof_syscall_data *data);

/*
 * Read /proc/kprof/pagefaults into structured data.
 * Returns 0 on success, -1 on error.
 */
int procfs_read_pagefaults(struct kprof_pagefault_data *data);

/*
 * Read /proc/[pid]/status for memory info.
 * Returns 0 on success, -1 on error.
 */
int procfs_read_mem_info(pid_t pid, struct proc_mem_info *info);

/*
 * Send a command to /proc/kprof/control.
 * cmd: "start <PID>", "stop", or "reset"
 * Returns 0 on success, -1 on error.
 */
int procfs_send_control(const char *cmd);

/*
 * Take a full snapshot of all available profiling data.
 * pid: process to read /proc/[pid]/status for (0 = skip)
 */
int procfs_take_snapshot(pid_t pid, struct kprof_snapshot *snap);

/*
 * Compute the difference between two snapshots (after - before).
 * Stores result in 'diff'.
 */
void procfs_diff_snapshots(const struct kprof_snapshot *before,
			   const struct kprof_snapshot *after,
			   struct kprof_snapshot *diff);

/*
 * Find a specific syscall by name in the data.
 * Returns pointer to entry or NULL if not found.
 */
const struct kprof_syscall_entry *
procfs_find_syscall(const struct kprof_syscall_data *data, const char *name);

#endif /* PROCFS_READER_H */
