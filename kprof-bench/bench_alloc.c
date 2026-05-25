/*
 * bench_alloc.c — Allocator benchmark
 *
 * Benchmarks malloc/free with various patterns:
 *   - Sequential: allocate N blocks, then free all
 *   - Random size: allocate with random sizes
 *   - Mixed: interleave alloc and free
 *   - Realloc: repeated realloc growing/shrinking
 *
 * When used with LD_PRELOAD=libkprofalloc.so, benchmarks the custom
 * allocator. Without LD_PRELOAD, benchmarks glibc malloc.
 *
 * Reads /proc/kprof/* before and after to measure syscalls and page faults.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "report.h"
#include "procfs_reader.h"

/* Default parameters */
#define DEFAULT_ALLOC_ITERS   100000
#define MAX_ALLOC_PTRS        100000

/* Allocation sizes to test */
static const size_t alloc_sizes[] = {
	16, 64, 256, 1024, 4096, 16384, 65536
};
#define NUM_ALLOC_SIZES  (sizeof(alloc_sizes) / sizeof(alloc_sizes[0]))

static inline uint64_t time_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/*
 * Pattern 1: Sequential — allocate N blocks of fixed size, then free all.
 */
static double bench_sequential_alloc(size_t block_size, uint64_t count)
{
	void **ptrs;
	uint64_t i;
	uint64_t start, end;

	if (count > MAX_ALLOC_PTRS)
		count = MAX_ALLOC_PTRS;

	ptrs = calloc(count, sizeof(void *));
	if (!ptrs) return -1;

	start = time_ns();

	/* Allocate */
	for (i = 0; i < count; i++) {
		ptrs[i] = malloc(block_size);
		if (ptrs[i])
			memset(ptrs[i], 0xAA, block_size > 16 ? 16 : block_size);
	}

	/* Free */
	for (i = 0; i < count; i++) {
		free(ptrs[i]);
	}

	end = time_ns();
	free(ptrs);

	/* Return ns per operation (alloc + free = 2 ops per iteration) */
	return (double)(end - start) / (double)(count * 2);
}

/*
 * Pattern 2: Random size — allocate with random sizes in [min, max].
 */
static double bench_random_size_alloc(size_t min_size, size_t max_size,
				      uint64_t count)
{
	void **ptrs;
	uint64_t i;
	uint64_t start, end;

	if (count > MAX_ALLOC_PTRS)
		count = MAX_ALLOC_PTRS;

	ptrs = calloc(count, sizeof(void *));
	if (!ptrs) return -1;

	start = time_ns();

	for (i = 0; i < count; i++) {
		size_t sz = min_size + ((size_t)rand() % (max_size - min_size + 1));
		ptrs[i] = malloc(sz);
		if (ptrs[i])
			((char *)ptrs[i])[0] = 0xBB; /* touch */
	}

	for (i = 0; i < count; i++) {
		free(ptrs[i]);
	}

	end = time_ns();
	free(ptrs);

	return (double)(end - start) / (double)(count * 2);
}

/*
 * Pattern 3: Mixed — interleave alloc and free (LIFO pattern).
 * Allocate a batch, free half, allocate more, free all.
 */
static double bench_mixed_alloc(size_t block_size, uint64_t count)
{
	void **ptrs;
	uint64_t i;
	uint64_t ops = 0;
	uint64_t start, end;
	uint64_t half = count / 2;

	if (count > MAX_ALLOC_PTRS)
		count = MAX_ALLOC_PTRS;
	half = count / 2;

	ptrs = calloc(count, sizeof(void *));
	if (!ptrs) return -1;

	start = time_ns();

	/* Phase 1: allocate all */
	for (i = 0; i < count; i++) {
		ptrs[i] = malloc(block_size);
		if (ptrs[i])
			((char *)ptrs[i])[0] = 0xCC;
		ops++;
	}

	/* Phase 2: free first half */
	for (i = 0; i < half; i++) {
		free(ptrs[i]);
		ptrs[i] = NULL;
		ops++;
	}

	/* Phase 3: re-allocate first half (reuse freed blocks?) */
	for (i = 0; i < half; i++) {
		ptrs[i] = malloc(block_size);
		if (ptrs[i])
			((char *)ptrs[i])[0] = 0xDD;
		ops++;
	}

	/* Phase 4: free all */
	for (i = 0; i < count; i++) {
		free(ptrs[i]);
		ops++;
	}

	end = time_ns();
	free(ptrs);

	return (double)(end - start) / (double)ops;
}

/*
 * Pattern 4: Realloc — repeatedly grow a buffer.
 */
static double bench_realloc(uint64_t count)
{
	uint64_t i;
	uint64_t start, end;
	void *ptr = NULL;
	size_t cur_size = 16;

	start = time_ns();

	for (i = 0; i < count; i++) {
		ptr = realloc(ptr, cur_size);
		if (ptr)
			((char *)ptr)[0] = 0xEE;
		cur_size += 64; /* grow by 64 bytes each time */

		/* Reset periodically to avoid huge allocations */
		if (cur_size > 1024 * 1024) {
			free(ptr);
			ptr = NULL;
			cur_size = 16;
		}
	}

	free(ptr);
	end = time_ns();

	return (double)(end - start) / (double)count;
}

/*
 * Run all allocator benchmarks.
 */
int bench_alloc_run(struct bench_result_set *rs, uint64_t iterations,
		    int use_procfs, const size_t *custom_sizes,
		    int num_custom_sizes)
{
	const size_t *sizes;
	int num_sizes;
	int i;
	struct kprof_snapshot snap_before, snap_after;

	memset(rs, 0, sizeof(*rs));
	strncpy(rs->name, "Allocator Benchmark", sizeof(rs->name));
	strncpy(rs->unit, "ns/op", sizeof(rs->unit));

	if (custom_sizes && num_custom_sizes > 0) {
		sizes = custom_sizes;
		num_sizes = num_custom_sizes;
	} else {
		sizes = alloc_sizes;
		num_sizes = NUM_ALLOC_SIZES;
	}

	srand(42);

	printf("  Running allocator benchmark (%llu iterations)...\n",
	       (unsigned long long)iterations);

	if (use_procfs)
		procfs_take_snapshot(getpid(), &snap_before);

	/* Sequential pattern for each size */
	for (i = 0; i < num_sizes && rs->num_results < MAX_BENCH_RESULTS; i++) {
		struct bench_result *r = &rs->results[rs->num_results];
		uint64_t start_time = time_ns();
		double lat = bench_sequential_alloc(sizes[i], iterations);

		snprintf(r->label, sizeof(r->label), "seq %zu B", sizes[i]);
		r->latency_ns = lat;
		r->throughput = (lat > 0) ? 1e9 / lat : 0;
		r->iterations = iterations * 2; /* alloc + free */
		r->duration_sec = (double)(time_ns() - start_time) / 1e9;
		rs->num_results++;

		printf("    %-20s  %.1f ns/op  (%.2fM ops/s)\n",
		       r->label, lat, r->throughput / 1e6);
	}

	/* Random size pattern */
	{
		struct bench_result *r = &rs->results[rs->num_results];
		uint64_t start_time = time_ns();
		double lat = bench_random_size_alloc(16, 4096, iterations);

		snprintf(r->label, sizeof(r->label), "random 16-4096 B");
		r->latency_ns = lat;
		r->throughput = (lat > 0) ? 1e9 / lat : 0;
		r->iterations = iterations * 2;
		r->duration_sec = (double)(time_ns() - start_time) / 1e9;
		rs->num_results++;

		printf("    %-20s  %.1f ns/op  (%.2fM ops/s)\n",
		       r->label, lat, r->throughput / 1e6);
	}

	/* Mixed pattern */
	{
		struct bench_result *r = &rs->results[rs->num_results];
		uint64_t start_time = time_ns();
		double lat = bench_mixed_alloc(256, iterations);

		snprintf(r->label, sizeof(r->label), "mixed 256 B");
		r->latency_ns = lat;
		r->throughput = (lat > 0) ? 1e9 / lat : 0;
		r->iterations = iterations * 3; /* alloc + free + realloc */
		r->duration_sec = (double)(time_ns() - start_time) / 1e9;
		rs->num_results++;

		printf("    %-20s  %.1f ns/op  (%.2fM ops/s)\n",
		       r->label, lat, r->throughput / 1e6);
	}

	/* Realloc pattern */
	{
		struct bench_result *r = &rs->results[rs->num_results];
		uint64_t start_time = time_ns();
		double lat = bench_realloc(iterations);

		snprintf(r->label, sizeof(r->label), "realloc grow");
		r->latency_ns = lat;
		r->throughput = (lat > 0) ? 1e9 / lat : 0;
		r->iterations = iterations;
		r->duration_sec = (double)(time_ns() - start_time) / 1e9;
		rs->num_results++;

		printf("    %-20s  %.1f ns/op  (%.2fM ops/s)\n",
		       r->label, lat, r->throughput / 1e6);
	}

	if (use_procfs) {
		procfs_take_snapshot(getpid(), &snap_after);
		procfs_diff_snapshots(&snap_before, &snap_after, &rs->procfs_diff);
		rs->has_procfs = 1;
	}

	printf("\n");
	return 0;
}
