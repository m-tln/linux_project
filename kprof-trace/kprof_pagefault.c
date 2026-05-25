// SPDX-License-Identifier: GPL-2.0
/*
 * kprof_pagefault.c — Page fault tracer using kernel tracepoints
 *
 * Uses the mm_fault tracepoint (trace_mm_page_alloc or page_fault_user)
 * instead of kprobes. Tracepoints are compiled as NOPs when disabled
 * and have near-zero overhead compared to kprobes (which use INT3 traps).
 *
 * Why NOT kprobes:
 *   kprobes on handle_mm_fault() insert a breakpoint (INT3) on every
 *   page fault, causing a trap + context switch. For memory-intensive
 *   workloads this completely destroys performance and skews benchmarks.
 *
 * Tracepoint approach:
 *   We hook into the "page_fault_user" tracepoint (or fallback to
 *   "handle_mm_fault" tracepoint) which fires after the fault is
 *   resolved. The tracepoint callback receives the fault address,
 *   error code, and whether it was major/minor.
 *
 * Alternative: We also read /proc/[pid]/stat for task-level fault
 * counters (min_flt, maj_flt fields) which requires zero kernel
 * overhead — this is done in the procfs_reader userspace component.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/tracepoint.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include "kprof.h"

/*
 * We use for_each_kernel_tracepoint() to find the page fault tracepoints
 * at runtime, similar to how we find sys_enter/sys_exit in kprof_syscall.c.
 *
 * Target tracepoints (in order of preference):
 *   1. "page_fault_user" — fires on user-space page faults (ideal)
 *   2. "mm_filemap_fault" — fires on file-backed page faults
 *
 * The page_fault_user tracepoint provides:
 *   - address: faulting virtual address
 *   - error_code: x86 error code (bit 0 = protection, bit 1 = write, etc.)
 *   - The tracepoint fires AFTER the fault is handled, so we can check
 *     the task's maj_flt/min_flt counters.
 *
 * Fallback: If no suitable tracepoint is found, we use a lightweight
 * approach: periodically sample /proc/[pid]/stat from userspace.
 */

/* Tracepoint pointers */
static struct tracepoint *tp_page_fault_user;
static bool tracepoint_registered;

/*
 * Snapshot of task fault counters — used to detect major vs minor.
 * We read task->min_flt and task->maj_flt which are maintained by
 * the kernel's fault handler without any instrumentation overhead.
 */

/*
 * page_fault_user tracepoint callback.
 *
 * Prototype varies by kernel version. On modern kernels (5.x+):
 *   void (*)(void *data, unsigned long address, unsigned long error_code)
 *
 * We simply increment our counters. The major/minor distinction
 * is determined by checking the task's maj_flt counter change.
 */
static void kprof_page_fault_cb(void *data, unsigned long address,
				struct pt_regs *regs, unsigned long error_code)
{
	unsigned long prev_maj;

	if (!kprof_should_trace())
		return;

	/*
	 * Determine major vs minor:
	 * We snapshot current->maj_flt before and check if it increased.
	 * This is a lightweight check — no locks needed since we're in
	 * the context of the faulting task.
	 *
	 * Note: This is a heuristic. For precise major/minor tracking,
	 * the userspace component reads /proc/[pid]/stat directly.
	 */
	prev_maj = atomic64_read(&kprof_state.pagefaults.last_majflt_snapshot);

	if (current->maj_flt > (unsigned long)prev_maj) {
		atomic64_inc(&kprof_state.pagefaults.major_faults);
		atomic64_set(&kprof_state.pagefaults.last_majflt_snapshot,
			     (s64)current->maj_flt);
	} else {
		atomic64_inc(&kprof_state.pagefaults.minor_faults);
	}

	/* Store last faulting address */
	atomic64_set(&kprof_state.pagefaults.last_address, (s64)address);
}

/*
 * Fallback: handle_mm_fault tracepoint (available on some kernels).
 * This fires with (vma, address, flags) — no return value, but we
 * can still count faults and use task->maj_flt for classification.
 */
static struct tracepoint *tp_mm_fault;

static void kprof_mm_fault_cb(void *data, struct vm_area_struct *vma,
			      unsigned long address, unsigned int flags)
{
	if (!kprof_should_trace())
		return;

	/*
	 * This tracepoint fires at entry to handle_mm_fault, before
	 * the fault is resolved. We count it as a fault event.
	 * Major/minor classification happens post-hoc via task counters.
	 */
	atomic64_inc(&kprof_state.pagefaults.minor_faults);
	atomic64_set(&kprof_state.pagefaults.last_address, (s64)address);
}

/*
 * Tracepoint lookup callback.
 */
static void lookup_tracepoints(struct tracepoint *tp, void *priv)
{
	if (!strcmp(tp->name, "page_fault_user"))
		tp_page_fault_user = tp;
	else if (!strcmp(tp->name, "handle_mm_fault"))
		tp_mm_fault = tp;
}

/*
 * Initialize page fault tracer.
 */
int kprof_pagefault_init(void)
{
	int ret;

	tp_page_fault_user = NULL;
	tp_mm_fault = NULL;
	tracepoint_registered = false;

	/* Find tracepoints */
	for_each_kernel_tracepoint(lookup_tracepoints, NULL);

	/* Try page_fault_user first (preferred — lowest overhead) */
	if (tp_page_fault_user) {
		ret = tracepoint_probe_register(tp_page_fault_user,
						kprof_page_fault_cb, NULL);
		if (ret == 0) {
			tracepoint_registered = true;
			pr_info(KPROF_NAME ": pagefault tracer initialized "
				"(tracepoint: page_fault_user)\n");
			return 0;
		}
		pr_warn(KPROF_NAME ": page_fault_user register failed (%d), "
			"trying fallback\n", ret);
	}

	/* Fallback: handle_mm_fault tracepoint */
	if (tp_mm_fault) {
		ret = tracepoint_probe_register(tp_mm_fault,
						kprof_mm_fault_cb, NULL);
		if (ret == 0) {
			tracepoint_registered = true;
			pr_info(KPROF_NAME ": pagefault tracer initialized "
				"(tracepoint: handle_mm_fault)\n");
			return 0;
		}
		pr_warn(KPROF_NAME ": handle_mm_fault register failed (%d)\n",
			ret);
	}

	/*
	 * No tracepoint available — not fatal.
	 * The userspace component can still read /proc/[pid]/stat
	 * for min_flt/maj_flt counters with zero kernel overhead.
	 */
	pr_warn(KPROF_NAME ": no page fault tracepoint found — "
		"pagefault counting disabled in kernel\n");
	pr_info(KPROF_NAME ": userspace can still read /proc/[pid]/stat "
		"for fault counters\n");
	return 0; /* non-fatal */
}

/*
 * Cleanup page fault tracer.
 */
void kprof_pagefault_exit(void)
{
	if (tracepoint_registered) {
		if (tp_page_fault_user)
			tracepoint_probe_unregister(tp_page_fault_user,
						    kprof_page_fault_cb, NULL);
		else if (tp_mm_fault)
			tracepoint_probe_unregister(tp_mm_fault,
						    kprof_mm_fault_cb, NULL);

		tracepoint_synchronize_unregister();
		tracepoint_registered = false;
	}

	pr_info(KPROF_NAME ": pagefault tracer stopped\n");
}
