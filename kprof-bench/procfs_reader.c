/*
 * procfs_reader.c — Implementation of procfs parsing for kprof-bench
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "procfs_reader.h"

#define KPROF_PROC_SYSCALLS   "/proc/kprof/syscalls"
#define KPROF_PROC_PAGEFAULTS "/proc/kprof/pagefaults"
#define KPROF_PROC_CONTROL    "/proc/kprof/control"
#define KPROF_PROC_CONFIG     "/proc/kprof/config"

#define LINE_BUF_SIZE  256

/*
 * Read /proc/kprof/syscalls
 *
 * Expected format (from kprof_proc.c):
 *   NR      NAME                    COUNT          TOTAL_NS        AVG_NS
 *   ------  --------------------  ------------  ----------------  ------------
 *   0       read                          1523           4821000          3165
 *   1       write                          892           2103000          2358
 */
int procfs_read_syscalls(struct kprof_syscall_data *data)
{
	FILE *fp;
	char line[LINE_BUF_SIZE];
	int idx = 0;

	memset(data, 0, sizeof(*data));

	fp = fopen(KPROF_PROC_SYSCALLS, "r");
	if (!fp) {
		fprintf(stderr, "procfs_reader: cannot open %s: %s\n",
			KPROF_PROC_SYSCALLS, strerror(errno));
		return -1;
	}

	/* Skip header lines (2 lines) */
	if (!fgets(line, sizeof(line), fp)) goto done;
	if (!fgets(line, sizeof(line), fp)) goto done;

	while (fgets(line, sizeof(line), fp) && idx < KPROF_MAX_SYSCALLS) {
		struct kprof_syscall_entry *e = &data->entries[idx];
		int nr;
		char name[32];
		unsigned long long count, total_ns, avg_ns;

		int matched = sscanf(line, "%d %31s %llu %llu %llu",
				     &nr, name, &count, &total_ns, &avg_ns);
		if (matched < 3)
			continue;

		e->nr = nr;
		strncpy(e->name, name, sizeof(e->name) - 1);
		e->name[sizeof(e->name) - 1] = '\0';
		e->count = count;
		e->total_ns = (matched >= 4) ? total_ns : 0;
		e->avg_ns = (matched >= 5) ? avg_ns : 0;
		idx++;
	}

done:
	data->num_entries = idx;
	fclose(fp);
	return 0;
}

/*
 * Read /proc/kprof/pagefaults
 *
 * Expected format:
 *   METRIC                VALUE
 *   --------------------  --------------------
 *   minor_faults          1203
 *   major_faults          0
 *   total_faults          1203
 *   last_fault_addr       0x7f4a2c001000
 */
int procfs_read_pagefaults(struct kprof_pagefault_data *data)
{
	FILE *fp;
	char line[LINE_BUF_SIZE];

	memset(data, 0, sizeof(*data));

	fp = fopen(KPROF_PROC_PAGEFAULTS, "r");
	if (!fp) {
		fprintf(stderr, "procfs_reader: cannot open %s: %s\n",
			KPROF_PROC_PAGEFAULTS, strerror(errno));
		return -1;
	}

	/* Skip header lines */
	if (!fgets(line, sizeof(line), fp)) goto done;
	if (!fgets(line, sizeof(line), fp)) goto done;

	while (fgets(line, sizeof(line), fp)) {
		char metric[32];
		unsigned long long value;

		if (sscanf(line, "%31s %llu", metric, &value) == 2) {
			if (strcmp(metric, "minor_faults") == 0)
				data->minor_faults = value;
			else if (strcmp(metric, "major_faults") == 0)
				data->major_faults = value;
			else if (strcmp(metric, "total_faults") == 0)
				data->total_faults = value;
		}

		/* Handle hex address separately */
		if (strstr(line, "last_fault_addr")) {
			char *hex = strstr(line, "0x");
			if (hex)
				data->last_fault_addr = strtoull(hex, NULL, 16);
		}
	}

done:
	fclose(fp);
	return 0;
}

/*
 * Read /proc/[pid]/status for memory info
 */
int procfs_read_mem_info(pid_t pid, struct proc_mem_info *info)
{
	FILE *fp;
	char path[64];
	char line[LINE_BUF_SIZE];

	memset(info, 0, sizeof(*info));

	snprintf(path, sizeof(path), "/proc/%d/status", (int)pid);
	fp = fopen(path, "r");
	if (!fp) {
		fprintf(stderr, "procfs_reader: cannot open %s: %s\n",
			path, strerror(errno));
		return -1;
	}

	while (fgets(line, sizeof(line), fp)) {
		long val;

		if (sscanf(line, "VmSize: %ld kB", &val) == 1)
			info->vm_size_kb = val;
		else if (sscanf(line, "VmRSS: %ld kB", &val) == 1)
			info->vm_rss_kb = val;
		else if (sscanf(line, "VmPeak: %ld kB", &val) == 1)
			info->vm_peak_kb = val;
		else if (sscanf(line, "VmSwap: %ld kB", &val) == 1)
			info->vm_swap_kb = val;
		else if (sscanf(line, "voluntary_ctxt_switches: %ld", &val) == 1)
			info->vol_ctxt_sw = val;
		else if (sscanf(line, "nonvoluntary_ctxt_switches: %ld", &val) == 1)
			info->invol_ctxt_sw = val;
	}

	fclose(fp);
	return 0;
}

/*
 * Send a command to /proc/kprof/control
 */
int procfs_send_control(const char *cmd)
{
	FILE *fp;

	fp = fopen(KPROF_PROC_CONTROL, "w");
	if (!fp) {
		fprintf(stderr, "procfs_reader: cannot open %s: %s\n",
			KPROF_PROC_CONTROL, strerror(errno));
		return -1;
	}

	fprintf(fp, "%s\n", cmd);
	fclose(fp);
	return 0;
}

/*
 * Take a full snapshot
 */
int procfs_take_snapshot(pid_t pid, struct kprof_snapshot *snap)
{
	memset(snap, 0, sizeof(*snap));

	/* Try reading kprof data (may fail if module not loaded) */
	if (procfs_read_syscalls(&snap->syscalls) == 0)
		snap->has_syscalls = true;

	if (procfs_read_pagefaults(&snap->pagefaults) == 0)
		snap->has_pagefaults = true;

	/* Read process memory info */
	if (pid > 0 && procfs_read_mem_info(pid, &snap->mem) == 0)
		snap->has_mem = true;

	return 0;
}

/*
 * Compute difference between two snapshots
 */
void procfs_diff_snapshots(const struct kprof_snapshot *before,
			   const struct kprof_snapshot *after,
			   struct kprof_snapshot *diff)
{
	int i;

	memset(diff, 0, sizeof(*diff));

	/* Diff syscalls */
	if (before->has_syscalls && after->has_syscalls) {
		diff->has_syscalls = true;
		diff->syscalls.num_entries = after->syscalls.num_entries;

		for (i = 0; i < after->syscalls.num_entries; i++) {
			const struct kprof_syscall_entry *ae = &after->syscalls.entries[i];
			diff->syscalls.entries[i] = *ae;

			/* Find matching entry in 'before' */
			const struct kprof_syscall_entry *be =
				procfs_find_syscall(&before->syscalls, ae->name);
			if (be) {
				diff->syscalls.entries[i].count =
					ae->count - be->count;
				diff->syscalls.entries[i].total_ns =
					ae->total_ns - be->total_ns;
			}

			/* Recompute avg */
			if (diff->syscalls.entries[i].count > 0)
				diff->syscalls.entries[i].avg_ns =
					diff->syscalls.entries[i].total_ns /
					diff->syscalls.entries[i].count;
		}
	}

	/* Diff pagefaults */
	if (before->has_pagefaults && after->has_pagefaults) {
		diff->has_pagefaults = true;
		diff->pagefaults.minor_faults =
			after->pagefaults.minor_faults - before->pagefaults.minor_faults;
		diff->pagefaults.major_faults =
			after->pagefaults.major_faults - before->pagefaults.major_faults;
		diff->pagefaults.total_faults =
			diff->pagefaults.minor_faults + diff->pagefaults.major_faults;
		diff->pagefaults.last_fault_addr = after->pagefaults.last_fault_addr;
	}

	/* Memory: just copy 'after' values (not diffable in a meaningful way) */
	if (after->has_mem) {
		diff->has_mem = true;
		diff->mem = after->mem;
	}
}

/*
 * Find a syscall entry by name
 */
const struct kprof_syscall_entry *
procfs_find_syscall(const struct kprof_syscall_data *data, const char *name)
{
	int i;

	for (i = 0; i < data->num_entries; i++) {
		if (strcmp(data->entries[i].name, name) == 0)
			return &data->entries[i];
	}
	return NULL;
}
