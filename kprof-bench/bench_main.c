/*
 * bench_main.c — kprof-bench CLI entry point
 *
 * Usage:
 *   kprof-bench --all                    Run all benchmarks
 *   kprof-bench --tlb                    TLB benchmark only
 *   kprof-bench --cache                  Cache benchmark only
 *   kprof-bench --alloc                  Allocator benchmark only
 *   kprof-bench --alloc --procfs         With procfs data collection
 *   kprof-bench --format json            JSON output
 *   kprof-bench --iters 500000           Custom iteration count
 *   kprof-bench --working-set 128        TLB working set in MB
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>

#include "report.h"
#include "procfs_reader.h"

/* Forward declarations for benchmark runners */
extern int bench_tlb_run(struct bench_result_set *rs, int working_set_mb,
			 uint64_t iterations, int use_procfs);
extern int bench_cache_run(struct bench_result_set *rs, uint64_t iterations,
			   int use_procfs);
extern int bench_alloc_run(struct bench_result_set *rs, uint64_t iterations,
			   int use_procfs, const size_t *custom_sizes,
			   int num_custom_sizes);

/* CLI options */
static struct option long_options[] = {
	{ "all",          no_argument,       NULL, 'a' },
	{ "tlb",          no_argument,       NULL, 't' },
	{ "cache",        no_argument,       NULL, 'c' },
	{ "alloc",        no_argument,       NULL, 'l' },
	{ "procfs",       no_argument,       NULL, 'p' },
	{ "format",       required_argument, NULL, 'f' },
	{ "iters",        required_argument, NULL, 'i' },
	{ "working-set",  required_argument, NULL, 'w' },
	{ "sizes",        required_argument, NULL, 's' },
	{ "help",         no_argument,       NULL, 'h' },
	{ NULL, 0, NULL, 0 }
};

static void print_usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [OPTIONS]\n"
		"\n"
		"Benchmarks:\n"
		"  -a, --all              Run all benchmarks\n"
		"  -t, --tlb              TLB miss benchmark\n"
		"  -c, --cache            Cache hierarchy benchmark\n"
		"  -l, --alloc            Allocator benchmark\n"
		"\n"
		"Options:\n"
		"  -p, --procfs           Read /proc/kprof/* for profiling data\n"
		"  -f, --format FMT       Output format: text (default) or json\n"
		"  -i, --iters N          Iterations per test (default: 1000000)\n"
		"  -w, --working-set MB   TLB working set size in MB (default: 256)\n"
		"  -s, --sizes LIST       Alloc sizes, comma-separated (e.g. 16,64,256)\n"
		"  -h, --help             Show this help\n"
		"\n"
		"Examples:\n"
		"  %s --all\n"
		"  %s --alloc --procfs --format json\n"
		"  %s --tlb --working-set 512 --iters 2000000\n",
		prog, prog, prog, prog);
}

/*
 * Parse comma-separated sizes string into array.
 * E.g. "16,64,256,1024" -> {16, 64, 256, 1024}
 */
#define MAX_CUSTOM_SIZES  32

static int parse_sizes(const char *str, size_t *sizes, int max_sizes)
{
	char *buf, *tok, *saveptr;
	int count = 0;

	buf = strdup(str);
	if (!buf) return 0;

	tok = strtok_r(buf, ",", &saveptr);
	while (tok && count < max_sizes) {
		sizes[count] = (size_t)atol(tok);
		if (sizes[count] > 0)
			count++;
		tok = strtok_r(NULL, ",", &saveptr);
	}

	free(buf);
	return count;
}

int main(int argc, char *argv[])
{
	int do_tlb = 0, do_cache = 0, do_alloc = 0;
	int use_procfs = 0;
	enum report_format fmt = REPORT_TEXT;
	uint64_t iterations = 1000000;
	int working_set_mb = 256;
	size_t custom_sizes[MAX_CUSTOM_SIZES];
	int num_custom_sizes = 0;
	int opt;

	while ((opt = getopt_long(argc, argv, "atclpf:i:w:s:h",
				  long_options, NULL)) != -1) {
		switch (opt) {
		case 'a':
			do_tlb = do_cache = do_alloc = 1;
			break;
		case 't':
			do_tlb = 1;
			break;
		case 'c':
			do_cache = 1;
			break;
		case 'l':
			do_alloc = 1;
			break;
		case 'p':
			use_procfs = 1;
			break;
		case 'f':
			if (strcmp(optarg, "json") == 0)
				fmt = REPORT_JSON;
			else
				fmt = REPORT_TEXT;
			break;
		case 'i':
			iterations = (uint64_t)atol(optarg);
			break;
		case 'w':
			working_set_mb = atoi(optarg);
			break;
		case 's':
			num_custom_sizes = parse_sizes(optarg, custom_sizes,
						       MAX_CUSTOM_SIZES);
			break;
		case 'h':
			print_usage(argv[0]);
			return 0;
		default:
			print_usage(argv[0]);
			return 1;
		}
	}

	/* Default: run all if nothing specified */
	if (!do_tlb && !do_cache && !do_alloc) {
		do_tlb = do_cache = do_alloc = 1;
	}

	printf("========================================\n");
	printf("  kprof-bench — Benchmark Suite\n");
	printf("  PID: %d\n", getpid());
	printf("  Iterations: %llu\n", (unsigned long long)iterations);
	printf("  Procfs: %s\n", use_procfs ? "enabled" : "disabled");
	printf("========================================\n\n");

	/* Run TLB benchmark */
	if (do_tlb) {
		struct bench_result_set rs;

		if (bench_tlb_run(&rs, working_set_mb, iterations,
				  use_procfs) == 0) {
			report_print_results(stdout, &rs, fmt);
		}
	}

	/* Run cache benchmark */
	if (do_cache) {
		struct bench_result_set rs;

		if (bench_cache_run(&rs, iterations, use_procfs) == 0) {
			report_print_results(stdout, &rs, fmt);
		}
	}

	/* Run allocator benchmark */
	if (do_alloc) {
		struct bench_result_set rs;

		if (bench_alloc_run(&rs, iterations, use_procfs,
				    num_custom_sizes > 0 ? custom_sizes : NULL,
				    num_custom_sizes) == 0) {
			report_print_results(stdout, &rs, fmt);
		}
	}

	printf("========================================\n");
	printf("  Benchmark complete.\n");
	printf("========================================\n");

	return 0;
}
