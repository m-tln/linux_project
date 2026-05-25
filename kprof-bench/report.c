/*
 * report.c — Benchmark report formatting implementation
 */

#include <stdio.h>
#include <string.h>

#include "report.h"

#define TABLE_WIDTH  72

/* ========================================================================
 * Text format helpers
 * ======================================================================== */

void report_print_separator(FILE *out, int width)
{
	int i;
	for (i = 0; i < width; i++)
		fputc('-', out);
	fputc('\n', out);
}

void report_print_title(FILE *out, const char *title, int width)
{
	int len = (int)strlen(title);
	int pad = (width - len - 4) / 2;
	int i;

	fprintf(out, "+");
	for (i = 0; i < width - 2; i++) fputc('-', out);
	fprintf(out, "+\n");

	fprintf(out, "|");
	for (i = 0; i < pad; i++) fputc(' ', out);
	fprintf(out, " %s ", title);
	for (i = 0; i < width - pad - len - 4; i++) fputc(' ', out);
	fprintf(out, "|\n");

	fprintf(out, "+");
	for (i = 0; i < width - 2; i++) fputc('-', out);
	fprintf(out, "+\n");
}

/* Format large numbers with commas: 1234567 -> "1,234,567" */
static void format_number(char *buf, size_t bufsize, uint64_t val)
{
	char tmp[32];
	int len, i, j, commas;

	snprintf(tmp, sizeof(tmp), "%llu", (unsigned long long)val);
	len = (int)strlen(tmp);
	commas = (len - 1) / 3;

	if ((size_t)(len + commas + 1) > bufsize) {
		snprintf(buf, bufsize, "%llu", (unsigned long long)val);
		return;
	}

	j = len + commas;
	buf[j] = '\0';
	for (i = len - 1; i >= 0; i--) {
		buf[--j] = tmp[i];
		if (i > 0 && (len - i) % 3 == 0)
			buf[--j] = ',';
	}
}

/* Format throughput with SI suffix */
static void format_throughput(char *buf, size_t bufsize, double val)
{
	if (val >= 1e9)
		snprintf(buf, bufsize, "%.2fG", val / 1e9);
	else if (val >= 1e6)
		snprintf(buf, bufsize, "%.2fM", val / 1e6);
	else if (val >= 1e3)
		snprintf(buf, bufsize, "%.2fK", val / 1e3);
	else
		snprintf(buf, bufsize, "%.1f", val);
}

/* ========================================================================
 * Text format output
 * ======================================================================== */

static void print_results_text(FILE *out, const struct bench_result_set *rs)
{
	int i;
	char num_buf[32], tp_buf[32];

	report_print_title(out, rs->name, TABLE_WIDTH);

	fprintf(out, "  %-20s  %14s  %12s  %12s\n",
		"Label", "Throughput", "Latency(ns)", "Iterations");
	report_print_separator(out, TABLE_WIDTH);

	for (i = 0; i < rs->num_results; i++) {
		const struct bench_result *r = &rs->results[i];

		format_throughput(tp_buf, sizeof(tp_buf), r->throughput);
		format_number(num_buf, sizeof(num_buf), r->iterations);

		fprintf(out, "  %-20s  %11s %s  %12.1f  %12s\n",
			r->label, tp_buf, rs->unit,
			r->latency_ns, num_buf);
	}

	fprintf(out, "\n");

	/* Print procfs data if available */
	if (rs->has_procfs) {
		report_print_procfs(out, &rs->procfs_diff, REPORT_TEXT);
	}
}

/* ========================================================================
 * JSON format output
 * ======================================================================== */

static void print_results_json(FILE *out, const struct bench_result_set *rs)
{
	int i;

	fprintf(out, "{\n");
	fprintf(out, "  \"name\": \"%s\",\n", rs->name);
	fprintf(out, "  \"unit\": \"%s\",\n", rs->unit);
	fprintf(out, "  \"results\": [\n");

	for (i = 0; i < rs->num_results; i++) {
		const struct bench_result *r = &rs->results[i];

		fprintf(out, "    {\n");
		fprintf(out, "      \"label\": \"%s\",\n", r->label);
		fprintf(out, "      \"throughput\": %.2f,\n", r->throughput);
		fprintf(out, "      \"latency_ns\": %.2f,\n", r->latency_ns);
		fprintf(out, "      \"iterations\": %llu,\n",
			(unsigned long long)r->iterations);
		fprintf(out, "      \"duration_sec\": %.6f\n", r->duration_sec);
		fprintf(out, "    }%s\n", (i < rs->num_results - 1) ? "," : "");
	}

	fprintf(out, "  ]");

	if (rs->has_procfs) {
		fprintf(out, ",\n  \"procfs\": {\n");

		if (rs->procfs_diff.has_pagefaults) {
			fprintf(out, "    \"minor_faults\": %llu,\n",
				(unsigned long long)rs->procfs_diff.pagefaults.minor_faults);
			fprintf(out, "    \"major_faults\": %llu,\n",
				(unsigned long long)rs->procfs_diff.pagefaults.major_faults);
		}

		if (rs->procfs_diff.has_mem) {
			fprintf(out, "    \"vm_rss_kb\": %ld,\n",
				rs->procfs_diff.mem.vm_rss_kb);
			fprintf(out, "    \"vm_peak_kb\": %ld\n",
				rs->procfs_diff.mem.vm_peak_kb);
		}

		fprintf(out, "  }");
	}

	fprintf(out, "\n}\n");
}

/* ========================================================================
 * Public API
 * ======================================================================== */

void report_print_results(FILE *out, const struct bench_result_set *rs,
			  enum report_format fmt)
{
	if (fmt == REPORT_JSON)
		print_results_json(out, rs);
	else
		print_results_text(out, rs);
}

void report_print_comparison(FILE *out, const struct bench_comparison *cmp,
			     enum report_format fmt)
{
	int i;

	if (fmt == REPORT_JSON) {
		fprintf(out, "{\n");
		fprintf(out, "  \"title\": \"%s\",\n", cmp->title);
		fprintf(out, "  \"%s\": ", cmp->label_a);
		print_results_json(out, &cmp->set_a);
		fprintf(out, ",\n  \"%s\": ", cmp->label_b);
		print_results_json(out, &cmp->set_b);
		fprintf(out, "\n}\n");
		return;
	}

	/* Text comparison table */
	report_print_title(out, cmp->title, TABLE_WIDTH);

	fprintf(out, "  %-20s  %16s  %16s  %10s\n",
		"Metric", cmp->label_a, cmp->label_b, "Ratio");
	report_print_separator(out, TABLE_WIDTH);

	/* Match results by label */
	for (i = 0; i < cmp->set_a.num_results && i < cmp->set_b.num_results; i++) {
		const struct bench_result *a = &cmp->set_a.results[i];
		const struct bench_result *b = &cmp->set_b.results[i];
		char tp_a[32], tp_b[32];
		double ratio = (b->throughput > 0) ? a->throughput / b->throughput : 0;

		format_throughput(tp_a, sizeof(tp_a), a->throughput);
		format_throughput(tp_b, sizeof(tp_b), b->throughput);

		fprintf(out, "  %-20s  %13s %s  %13s %s  %9.2fx\n",
			a->label,
			tp_a, cmp->set_a.unit,
			tp_b, cmp->set_b.unit,
			ratio);
	}

	fprintf(out, "\n");

	/* Print procfs comparison if available */
	if (cmp->set_a.has_procfs && cmp->set_b.has_procfs) {
		const struct kprof_snapshot *da = &cmp->set_a.procfs_diff;
		const struct kprof_snapshot *db = &cmp->set_b.procfs_diff;

		fprintf(out, "  %-20s  %16s  %16s\n",
			"Procfs Metric", cmp->label_a, cmp->label_b);
		report_print_separator(out, TABLE_WIDTH);

		if (da->has_pagefaults && db->has_pagefaults) {
			fprintf(out, "  %-20s  %16llu  %16llu\n", "Minor faults",
				(unsigned long long)da->pagefaults.minor_faults,
				(unsigned long long)db->pagefaults.minor_faults);
			fprintf(out, "  %-20s  %16llu  %16llu\n", "Major faults",
				(unsigned long long)da->pagefaults.major_faults,
				(unsigned long long)db->pagefaults.major_faults);
		}

		if (da->has_syscalls && db->has_syscalls) {
			/* Show key syscalls: brk, mmap, munmap */
			const char *key_syscalls[] = {"brk", "mmap", "munmap", NULL};
			int j;

			for (j = 0; key_syscalls[j]; j++) {
				const struct kprof_syscall_entry *ea =
					procfs_find_syscall(&da->syscalls, key_syscalls[j]);
				const struct kprof_syscall_entry *eb =
					procfs_find_syscall(&db->syscalls, key_syscalls[j]);

				fprintf(out, "  %-20s  %16llu  %16llu\n",
					key_syscalls[j],
					ea ? (unsigned long long)ea->count : 0ULL,
					eb ? (unsigned long long)eb->count : 0ULL);
			}
		}

		if (da->has_mem && db->has_mem) {
			fprintf(out, "  %-20s  %13ld KB  %13ld KB\n", "VmRSS",
				da->mem.vm_rss_kb, db->mem.vm_rss_kb);
			fprintf(out, "  %-20s  %13ld KB  %13ld KB\n", "VmPeak",
				da->mem.vm_peak_kb, db->mem.vm_peak_kb);
		}

		fprintf(out, "\n");
	}
}

void report_print_procfs(FILE *out, const struct kprof_snapshot *snap,
			 enum report_format fmt)
{
	(void)fmt; /* text only for now */

	fprintf(out, "  Procfs Data:\n");

	if (snap->has_pagefaults) {
		fprintf(out, "    Minor page faults: %llu\n",
			(unsigned long long)snap->pagefaults.minor_faults);
		fprintf(out, "    Major page faults: %llu\n",
			(unsigned long long)snap->pagefaults.major_faults);
	}

	if (snap->has_syscalls) {
		const char *key[] = {"brk", "mmap", "munmap", "read", "write", NULL};
		int j;

		for (j = 0; key[j]; j++) {
			const struct kprof_syscall_entry *e =
				procfs_find_syscall(&snap->syscalls, key[j]);
			if (e && e->count > 0)
				fprintf(out, "    %s() calls: %llu (avg %llu ns)\n",
					key[j],
					(unsigned long long)e->count,
					(unsigned long long)e->avg_ns);
		}
	}

	if (snap->has_mem) {
		fprintf(out, "    VmRSS:  %ld KB\n", snap->mem.vm_rss_kb);
		fprintf(out, "    VmPeak: %ld KB\n", snap->mem.vm_peak_kb);
	}

	fprintf(out, "\n");
}