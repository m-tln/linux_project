/*
 * bench_tlb.c — TLB miss benchmark
 *
 * Measures the impact of TLB misses by accessing memory with different
 * stride patterns. Larger strides cross more page boundaries, causing
 * more TLB misses and slower access times.
 *
 * Test methodology:
 *   1. Allocate a large array (working set)
 *   2. Access elements with a fixed stride (4KB, 2MB, etc.)
 *   3. Measure time per access
 *   4. Compare: small stride (TLB-friendly) vs large stride (TLB-hostile)
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>

#include "report.h"
#include "procfs_reader.h"

/*
 * Compiler barrier: prevents the compiler from optimizing away
 * memory accesses in benchmark loops. Without this, -O2/-O3 will
 * detect that 'sink' is never used meaningfully and delete the loop.
 */
#define COMPILER_BARRIER(val) \
	asm volatile("" : "+r"(val) : : "memory")

/* Default parameters */
#define DEFAULT_WORKING_SET_MB  256
#define DEFAULT_ITERATIONS      (1000 * 1000)
#define NUM_WARMUP_ITERS        1000

/* Page sizes to test as strides */
struct tlb_test_config {
	const char *label;
	size_t      stride;       /* bytes between accesses */
};

static const struct tlb_test_config tlb_tests[] = {
	{ "64B (cache line)",    64 },
	{ "4KB (page)",          4096 },
	{ "64KB",                64 * 1024 },
	{ "2MB (huge page)",     2 * 1024 * 1024 },
	{ "16MB",                16 * 1024 * 1024 },
};

#define NUM_TLB_TESTS  (sizeof(tlb_tests) / sizeof(tlb_tests[0]))

/*
 * Get current time in nanoseconds (monotonic clock)
 */
static inline uint64_t time_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/*
 * Run a single TLB benchmark with the given stride.
 * Returns average access time in nanoseconds.
 */
static double run_tlb_test(volatile char *buf, size_t buf_size,
			   size_t stride, uint64_t iterations)
{
	uint64_t start, end;
	uint64_t i;
	size_t offset = 0;
	volatile char sink = 0;

	/* Warmup: touch pages to avoid cold-start effects */
	for (i = 0; i < NUM_WARMUP_ITERS && i * stride < buf_size; i++) {
		sink += buf[(i * stride) % buf_size];
	}
	COMPILER_BARRIER(sink);

	/* Timed run */
	start = time_ns();

	for (i = 0; i < iterations; i++) {
		sink += buf[offset];
		offset += stride;
		if (offset >= buf_size)
			offset = 0;
		/* Barrier every 1024 iterations to prevent loop vectorization
		 * from hiding the memory access pattern */
		if ((i & 0x3FF) == 0)
			COMPILER_BARRIER(sink);
	}

	end = time_ns();
	COMPILER_BARRIER(sink);
	return (double)(end - start) / (double)iterations;
}

/*
 * Run all TLB benchmarks and populate result set.
 */
int bench_tlb_run(struct bench_result_set *rs, int working_set_mb,
		  uint64_t iterations, int use_procfs)
{
	size_t buf_size;
	volatile char *buf;
	size_t i;
	struct kprof_snapshot snap_before, snap_after;

	memset(rs, 0, sizeof(*rs));
	snprintf(rs->name, sizeof(rs->name), "TLB Benchmark (%d MB working set)",
		 working_set_mb);
	strncpy(rs->unit, "ns/access", sizeof(rs->unit));

	buf_size = (size_t)working_set_mb * 1024 * 1024;

	/* Allocate with mmap for page-aligned memory */
	buf = mmap(NULL, buf_size, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (buf == MAP_FAILED) {
		perror("bench_tlb: mmap failed");
		return -1;
	}

	/* Pre-fault all pages */
	memset((void *)buf, 0xAA, buf_size);

	printf("  Running TLB benchmark (%d MB, %llu iterations per test)...\n",
	       working_set_mb, (unsigned long long)iterations);

	for (i = 0; i < NUM_TLB_TESTS; i++) {
		const struct tlb_test_config *tc = &tlb_tests[i];
		struct bench_result *r = &rs->results[rs->num_results];
		double latency;
		uint64_t start_time;

		/* Skip if stride > buffer */
		if (tc->stride >= buf_size) {
			printf("    [skip] %s (stride > buffer)\n", tc->label);
			continue;
		}

		if (use_procfs)
			procfs_take_snapshot(getpid(), &snap_before);

		start_time = time_ns();
		latency = run_tlb_test(buf, buf_size, tc->stride, iterations);
		double duration = (double)(time_ns() - start_time) / 1e9;

		strncpy(r->label, tc->label, sizeof(r->label) - 1);
		r->latency_ns = latency;
		r->throughput = 1e9 / latency; /* accesses per second */
		r->iterations = iterations;
		r->duration_sec = duration;
		rs->num_results++;

		printf("    %-20s  %.1f ns/access  (%.2fM access/s)\n",
		       tc->label, latency, r->throughput / 1e6);

		if (use_procfs) {
			procfs_take_snapshot(getpid(), &snap_after);
			/* Store last test's procfs diff */
			procfs_diff_snapshots(&snap_before, &snap_after,
					      &rs->procfs_diff);
			rs->has_procfs = 1;
		}
	}

	munmap((void *)buf, buf_size);
	return 0;
}
