/*
 * report.h — Benchmark report formatting API
 *
 * Supports text table and JSON output formats.
 */

#ifndef REPORT_H
#define REPORT_H

#include <stdio.h>
#include <stdint.h>
#include "procfs_reader.h"

/* Output format */
enum report_format {
	REPORT_TEXT,
	REPORT_JSON,
};

/*
 * Single benchmark result entry (one row in the report)
 */
struct bench_result {
	char         label[64];       /* e.g. "16 bytes", "4KB stride" */
	double       throughput;      /* ops/sec or MB/s */
	double       latency_ns;     /* average latency in ns */
	uint64_t     iterations;     /* number of iterations */
	double       duration_sec;   /* total duration in seconds */
};

/*
 * Benchmark result set (one benchmark run)
 */
#define MAX_BENCH_RESULTS  64

struct bench_result_set {
	char                  name[64];       /* benchmark name */
	char                  unit[16];       /* throughput unit: "ops/s", "MB/s" */
	struct bench_result   results[MAX_BENCH_RESULTS];
	int                   num_results;

	/* Optional procfs data */
	struct kprof_snapshot procfs_diff;    /* diff snapshot (after - before) */
	int                   has_procfs;
};

/*
 * Comparison report: two result sets side by side
 */
struct bench_comparison {
	char                    title[128];
	struct bench_result_set set_a;       /* e.g. "myalloc" */
	struct bench_result_set set_b;       /* e.g. "glibc" */
	char                    label_a[32]; /* column header for set_a */
	char                    label_b[32]; /* column header for set_b */
};

/*
 * Print a single result set as a table.
 */
void report_print_results(FILE *out, const struct bench_result_set *rs,
			  enum report_format fmt);

/*
 * Print a comparison report (two result sets side by side).
 */
void report_print_comparison(FILE *out, const struct bench_comparison *cmp,
			     enum report_format fmt);

/*
 * Print procfs snapshot data (syscalls, pagefaults, memory).
 */
void report_print_procfs(FILE *out, const struct kprof_snapshot *snap,
			 enum report_format fmt);

/*
 * Print a horizontal separator line.
 */
void report_print_separator(FILE *out, int width);

/*
 * Print a centered title in a box.
 */
void report_print_title(FILE *out, const char *title, int width);

#endif /* REPORT_H */
