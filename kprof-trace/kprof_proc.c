// SPDX-License-Identifier: GPL-2.0
/*
 * kprof_proc.c — Procfs interface for kprof
 *
 * Creates /proc/kprof/ directory with the following files:
 *   - syscalls    (read-only)  — syscall statistics table
 *   - pagefaults  (read-only)  — page fault statistics
 *   - config      (read-only)  — current configuration
 *   - control     (write-only) — commands: start <PID>, stop, reset
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <asm/unistd.h>

#include "kprof.h"

/* Procfs directory entry */
struct proc_dir_entry *kprof_proc_dir;

/*
 * Syscall name table (x86_64).
 * We only name the most common ones; the rest show as "syscall_NNN".
 */
static const char *syscall_names[] = {
	[__NR_read]           = "read",
	[__NR_write]          = "write",
	[__NR_open]           = "open",
	[__NR_close]          = "close",
	[__NR_newstat]        = "stat",
	[__NR_newfstat]       = "fstat",
	[__NR_newlstat]       = "lstat",
	[__NR_poll]           = "poll",
	[__NR_lseek]          = "lseek",
	[__NR_mmap]           = "mmap",
	[__NR_mprotect]       = "mprotect",
	[__NR_munmap]         = "munmap",
	[__NR_brk]            = "brk",
	[__NR_ioctl]          = "ioctl",
	[__NR_access]         = "access",
	[__NR_pipe]           = "pipe",
	[__NR_select]         = "select",
	[__NR_sched_yield]    = "sched_yield",
	[__NR_mremap]         = "mremap",
	[__NR_madvise]        = "madvise",
	[__NR_dup]            = "dup",
	[__NR_dup2]           = "dup2",
	[__NR_nanosleep]      = "nanosleep",
	[__NR_getpid]         = "getpid",
	[__NR_socket]         = "socket",
	[__NR_connect]        = "connect",
	[__NR_accept]         = "accept",
	[__NR_sendto]         = "sendto",
	[__NR_recvfrom]       = "recvfrom",
	[__NR_bind]           = "bind",
	[__NR_listen]         = "listen",
	[__NR_clone]          = "clone",
	[__NR_fork]           = "fork",
	[__NR_vfork]          = "vfork",
	[__NR_execve]         = "execve",
	[__NR_exit]           = "exit",
	[__NR_wait4]          = "wait4",
	[__NR_kill]           = "kill",
	[__NR_fcntl]          = "fcntl",
	[__NR_flock]          = "flock",
	[__NR_fsync]          = "fsync",
	[__NR_getcwd]         = "getcwd",
	[__NR_chdir]          = "chdir",
	[__NR_mkdir]          = "mkdir",
	[__NR_rmdir]          = "rmdir",
	[__NR_unlink]         = "unlink",
	[__NR_readlink]       = "readlink",
	[__NR_chmod]          = "chmod",
	[__NR_chown]          = "chown",
	[__NR_getuid]         = "getuid",
	[__NR_getgid]         = "getgid",
	[__NR_geteuid]        = "geteuid",
	[__NR_getegid]        = "getegid",
	[__NR_getppid]        = "getppid",
	[__NR_epoll_create]   = "epoll_create",
	[__NR_epoll_ctl]      = "epoll_ctl",
	[__NR_epoll_wait]     = "epoll_wait",
	[__NR_openat]         = "openat",
	[__NR_newfstatat]     = "newfstatat",
	[__NR_exit_group]     = "exit_group",
	[__NR_futex]          = "futex",
	[__NR_set_tid_address] = "set_tid_address",
	[__NR_clock_gettime]  = "clock_gettime",
	[__NR_clock_nanosleep] = "clock_nanosleep",
};

#define SYSCALL_NAMES_SIZE  ARRAY_SIZE(syscall_names)

static const char *get_syscall_name(int nr)
{
	if (nr >= 0 && nr < (int)SYSCALL_NAMES_SIZE && syscall_names[nr])
		return syscall_names[nr];
	return NULL;
}

/* ========================================================================
 * /proc/kprof/syscalls — read-only, shows syscall statistics
 * ======================================================================== */

static int kprof_syscalls_show(struct seq_file *m, void *v)
{
	int i;
	s64 count, total_ns;

	seq_printf(m, "%-6s  %-20s  %12s  %16s  %12s\n",
		   "NR", "NAME", "COUNT", "TOTAL_NS", "AVG_NS");
	seq_printf(m, "%-6s  %-20s  %12s  %16s  %12s\n",
		   "------", "--------------------",
		   "------------", "----------------",
		   "------------");

	for (i = 0; i < KPROF_MAX_SYSCALLS; i++) {
		count = atomic64_read(&kprof_state.syscalls[i].count);
		if (count == 0)
			continue;

		total_ns = atomic64_read(&kprof_state.syscalls[i].total_ns);

		seq_printf(m, "%-6d  %-20s  %12lld  %16lld  %12lld\n",
			   i,
			   get_syscall_name(i) ? get_syscall_name(i) : "unknown",
			   count,
			   total_ns,
			   count > 0 ? total_ns / count : 0);
	}

	return 0;
}

static int kprof_syscalls_open(struct inode *inode, struct file *file)
{
	return single_open(file, kprof_syscalls_show, NULL);
}

static const struct proc_ops kprof_syscalls_ops = {
	.proc_open    = kprof_syscalls_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};

/* ========================================================================
 * /proc/kprof/pagefaults — read-only, shows page fault statistics
 * ======================================================================== */

static int kprof_pagefaults_show(struct seq_file *m, void *v)
{
	s64 minor, major, last_addr;

	minor     = atomic64_read(&kprof_state.pagefaults.minor_faults);
	major     = atomic64_read(&kprof_state.pagefaults.major_faults);
	last_addr = atomic64_read(&kprof_state.pagefaults.last_address);

	seq_printf(m, "%-20s  %s\n", "METRIC", "VALUE");
	seq_printf(m, "%-20s  %s\n", "--------------------", "--------------------");
	seq_printf(m, "%-20s  %lld\n", "minor_faults", minor);
	seq_printf(m, "%-20s  %lld\n", "major_faults", major);
	seq_printf(m, "%-20s  %lld\n", "total_faults", minor + major);
	seq_printf(m, "%-20s  0x%llx\n", "last_fault_addr", (unsigned long long)last_addr);

	return 0;
}

static int kprof_pagefaults_open(struct inode *inode, struct file *file)
{
	return single_open(file, kprof_pagefaults_show, NULL);
}

static const struct proc_ops kprof_pagefaults_ops = {
	.proc_open    = kprof_pagefaults_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};

/* ========================================================================
	* /proc/kprof/config — read-only, shows current configuration
	* ======================================================================== */

static int kprof_config_show(struct seq_file *m, void *v)
{
	unsigned long flags;
	pid_t pid;
	bool active;

	spin_lock_irqsave(&kprof_state.state_lock, flags);
	pid = kprof_state.target_pid;
	active = kprof_state.active;
	spin_unlock_irqrestore(&kprof_state.state_lock, flags);

	seq_printf(m, "target_pid: %d\n", pid);
	seq_printf(m, "active: %s\n", active ? "yes" : "no");

	return 0;
}

static int kprof_config_open(struct inode *inode, struct file *file)
{
	return single_open(file, kprof_config_show, NULL);
}

static const struct proc_ops kprof_config_ops = {
	.proc_open    = kprof_config_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};

/* ========================================================================
	* /proc/kprof/control — write-only, accepts commands:
	*   "start <PID>"  — start tracing the given PID
	*   "stop"         — stop tracing
	*   "reset"        — reset all statistics to zero
	* ======================================================================== */

#define KPROF_CONTROL_BUFSIZE  64

static ssize_t kprof_control_write(struct file *file, const char __user *ubuf,
				   size_t count, loff_t *ppos)
{
	char buf[KPROF_CONTROL_BUFSIZE];
	unsigned long flags;
	size_t len;
	pid_t pid;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, ubuf, len))
		return -EFAULT;
	buf[len] = '\0';

	/* Strip trailing newline */
	if (len > 0 && buf[len - 1] == '\n')
		buf[len - 1] = '\0';

	if (strncmp(buf, "start ", 6) == 0) {
		/* "start <PID>" */
		if (kstrtoint(buf + 6, 10, &pid) != 0 || pid <= 0) {
			pr_err(KPROF_NAME ": invalid PID in 'start' command\n");
			return -EINVAL;
		}

		spin_lock_irqsave(&kprof_state.state_lock, flags);
		kprof_state.target_pid = pid;
		kprof_state.active = true;
		spin_unlock_irqrestore(&kprof_state.state_lock, flags);

		pr_info(KPROF_NAME ": tracing started for PID %d\n", pid);

	} else if (strcmp(buf, "stop") == 0) {
		spin_lock_irqsave(&kprof_state.state_lock, flags);
		kprof_state.active = false;
		spin_unlock_irqrestore(&kprof_state.state_lock, flags);

		pr_info(KPROF_NAME ": tracing stopped\n");

	} else if (strcmp(buf, "reset") == 0) {
		kprof_reset_stats();
		pr_info(KPROF_NAME ": statistics reset\n");

	} else {
		pr_err(KPROF_NAME ": unknown command '%s'\n", buf);
		return -EINVAL;
	}

	return count;
}

static const struct proc_ops kprof_control_ops = {
	.proc_write   = kprof_control_write,
	.proc_lseek   = noop_llseek,
};

/* ========================================================================
	* Init / Exit
	* ======================================================================== */

int kprof_proc_init(void)
{
	struct proc_dir_entry *entry;

	/* Create /proc/kprof/ directory */
	kprof_proc_dir = proc_mkdir(KPROF_PROC_DIR, NULL);
	if (!kprof_proc_dir) {
		pr_err(KPROF_NAME ": failed to create /proc/%s\n", KPROF_PROC_DIR);
		return -ENOMEM;
	}

	/* /proc/kprof/syscalls */
	entry = proc_create("syscalls", 0444, kprof_proc_dir, &kprof_syscalls_ops);
	if (!entry)
		goto fail;

	/* /proc/kprof/pagefaults */
	entry = proc_create("pagefaults", 0444, kprof_proc_dir, &kprof_pagefaults_ops);
	if (!entry)
		goto fail;

	/* /proc/kprof/config */
	entry = proc_create("config", 0444, kprof_proc_dir, &kprof_config_ops);
	if (!entry)
		goto fail;

	/* /proc/kprof/control */
	entry = proc_create("control", 0200, kprof_proc_dir, &kprof_control_ops);
	if (!entry)
		goto fail;

	pr_info(KPROF_NAME ": procfs interface created at /proc/%s/\n", KPROF_PROC_DIR);
	return 0;

fail:
	pr_err(KPROF_NAME ": failed to create procfs entries\n");
	kprof_proc_exit();
	return -ENOMEM;
}

void kprof_proc_exit(void)
{
	if (kprof_proc_dir) {
		remove_proc_subtree(KPROF_PROC_DIR, NULL);
		kprof_proc_dir = NULL;
	}

	pr_info(KPROF_NAME ": procfs interface removed\n");
}