// SPDX-License-Identifier: GPL-2.0
/*
 * kprof_pagefault.c — Page fault tracer using kprobes
 *
 * Hooks into handle_mm_fault() to count minor and major page faults
 * for the target PID.
 *
 * handle_mm_fault() is the main entry point for page fault handling
 * in the Linux kernel. Its return value (vm_fault_t) tells us whether
 * the fault was minor (page in memory, just PTE update) or major
 * (page had to be read from disk).
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/ktime.h>

#include "kprof.h"

/*
 * We use a kretprobe on handle_mm_fault() so we can inspect both
 * the input (address) and the return value (fault type).
 *
 * handle_mm_fault signature (Linux 6.x):
 *   vm_fault_t handle_mm_fault(struct vm_area_struct *vma,
 *                              unsigned long address,
 *                              unsigned int flags,
 *                              struct pt_regs *regs);
 */

/* Per-instance data stored between entry and return */
struct kprof_pf_data {
	unsigned long address;
	u64           entry_ns;
};

/*
 * Entry handler — called when handle_mm_fault() is entered.
 * We save the faulting address for use in the return handler.
 */
static int kprof_pf_entry(struct kretprobe_instance *ri,
			   struct pt_regs *regs)
{
	struct kprof_pf_data *data;

	if (!kprof_should_trace())
		return 1; /* skip this instance (don't call ret handler) */

	data = (struct kprof_pf_data *)ri->data;

	/*
	 * On x86_64, function arguments are in registers:
	 *   rdi = vma, rsi = address, rdx = flags, rcx = regs
	 * We want the address (2nd argument).
	 */
#ifdef CONFIG_X86_64
	data->address = regs->si;
#elif defined(CONFIG_ARM64)
	data->address = regs->regs[1];
#else
	data->address = 0; /* fallback */
#endif

	data->entry_ns = ktime_get_ns();
	return 0;
}

/*
 * Return handler — called when handle_mm_fault() returns.
 * We classify the fault as minor or major based on the return value.
 */
static int kprof_pf_ret(struct kretprobe_instance *ri,
			 struct pt_regs *regs)
{
	struct kprof_pf_data *data = (struct kprof_pf_data *)ri->data;
	unsigned long retval;

	retval = regs_return_value(regs);

	/*
	 * vm_fault_t flags (from include/linux/mm_types.h):
	 *   VM_FAULT_MAJOR  = 0x0004 — major fault (disk I/O)
	 *   VM_FAULT_ERROR  = various error bits
	 *
	 * If VM_FAULT_MAJOR is set → major fault
	 * Otherwise (and no error) → minor fault
	 */
	if (retval & VM_FAULT_ERROR) {
		/* Error fault — don't count */
		return 0;
	}

	if (retval & VM_FAULT_MAJOR) {
		atomic64_inc(&kprof_state.pagefaults.major_faults);
	} else {
		atomic64_inc(&kprof_state.pagefaults.minor_faults);
	}

	/* Store last faulting address (informational) */
	atomic64_set(&kprof_state.pagefaults.last_address,
		     (s64)data->address);

	return 0;
}

static struct kretprobe kprof_pf_kretprobe = {
	.handler        = kprof_pf_ret,
	.entry_handler  = kprof_pf_entry,
	.data_size      = sizeof(struct kprof_pf_data),
	.maxactive      = 64, /* max concurrent probed instances */
	.kp.symbol_name = "handle_mm_fault",
};

static bool kretprobe_registered;

/*
 * Initialize page fault tracer.
 */
int kprof_pagefault_init(void)
{
	int ret;

	kretprobe_registered = false;

	ret = register_kretprobe(&kprof_pf_kretprobe);
	if (ret) {
		pr_err(KPROF_NAME ": failed to register kretprobe on handle_mm_fault (err=%d)\n", ret);
		pr_err(KPROF_NAME ": is CONFIG_KPROBES enabled? Is handle_mm_fault available?\n");
		return ret;
	}

	kretprobe_registered = true;
	pr_info(KPROF_NAME ": pagefault tracer initialized (kretprobe on handle_mm_fault)\n");
	return 0;
}

/*
 * Cleanup page fault tracer.
 */
void kprof_pagefault_exit(void)
{
	if (kretprobe_registered) {
		unregister_kretprobe(&kprof_pf_kretprobe);
		kretprobe_registered = false;

		/* Log missed probes (if any were dropped due to maxactive) */
		if (kprof_pf_kretprobe.nmissed > 0)
			pr_info(KPROF_NAME ": pagefault kretprobe missed %lu events\n",
				kprof_pf_kretprobe.nmissed);
	}

	pr_info(KPROF_NAME ": pagefault tracer stopped\n");
}
