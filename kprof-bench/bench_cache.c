/*
 * bench_cache.c — Cache hierarchy benchmark
 *
 * Measures memory access latency at different working set sizes to
 * reveal the cache hierarchy (L1, L2, L3, main memory).
 *
 * Two access patterns:
 *   - Sequential: linear scan (prefetcher-friendly)
 *   - Random: pointer-chasing (defeats prefetcher, measures true latency)
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

/* Working set sizes to test */
struct cache_test_config {
	const char *label;
	size_t      size_kb;
};

static const struct cache_test_config cache_tests[] = {
	{ "8 KB (< L1)",        8 },
	{ "32 KB (L1)",         32 },
	{ "128 KB (L2)",        128 },
	{ "256 KB (L2)",        256 },
	{ "1 MB (L2/L3)",       1024 },
	{ "4 MB (L3)",          4096 },
	{ "8 MB (L3)",          8192 },
	{ "32 MB (> L3)",       32768 },
	{ "64 MB (RAM)",        65536 },
};

#define NUM_CACHE_TESTS  (sizeof(cache_tests) / sizeof(cache_tests[0]))
#define CACHE_LINE_SIZE  64

static inline uint64_t time_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/*
 * Sequential access benchmark.
 * Reads every cache line in the buffer sequentially.
 */
static double bench_sequential(volatile char *buf, size_t size, uint64_t iters)
{
	uint64_t start, end;
	uint64_t i;
	size_t offset = 0;
	volatile char sink = 0;

	start = time_ns();

	for (i = 0; i < iters; i++) {
		sink += buf[offset];
		offset += CACHE_LINE_SIZE;
		if (offset >= size)
			offset = 0;
	}

	end = time_ns();
	(void)sink;
	return (double)(end - start) / (double)iters;
}

/*
 * Random access benchmark (pointer chasing).
 *
 * Creates a random permutation of indices, then follows the chain.
 * This defeats hardware prefetchers and measures true cache latency.
 */
static void build_random_chain(size_t *chain, size_t num_elements)
{
	size_t i, j;

	/* Initialize identity permutation */
	for (i = 0; i < num_elements; i++)
		chain[i] = i;

	/* Fisher-Yates shuffle */
	for (i = num_elements - 1; i > 0; i--) {
		j = (size_t)rand() % (i + 1);
		size_t tmp = chain[i];
		chain[i] = chain[j];
		chain[j] = tmp;
	}

	/* Convert permutation to pointer chain:
	 * chain[i] = next index to visit after i */
	size_t *perm = malloc(num_elements * sizeof(size_t));
	if (!perm) return;
	memcpy(perm, chain, num_elements * sizeof(size_t));

	/* Build cycle: perm[0] -> perm[1] -> ... -> perm[n-1] -> perm[0] */
	for (i = 0; i < num_elements - 1; i++)
		chain[perm[i]] = perm[i + 1];
	chain[perm[num_elements - 1]] = perm[0];

	free(perm);
}

static double bench_random(volatile size_t *buf, size_t num_elements,
			   uint64_t iters)
{
	uint64_t start, end;
	uint64_t i;
	size_t idx = 0;

	start = time_ns();

	for (i = 0; i < iters; i++) {
		idx = buf[idx];
	}

	end = time_ns();

	/* Prevent dead code elimination */
	if (idx == (size_t)-1)
		printf("never\n");

	return (double)(end - start) / (double)iters;
}

/*
 * Run all cache benchmarks.
 */
int bench_cache_run(struct bench_result_set *rs, uint64_t iterations,
		    int use_procfs)
{
	size_t i;
	struct kprof_snapshot snap_before, snap_after;

	memset(rs, 0, sizeof(*rs));
	strncpy(rs->name, "Cache Hierarchy Benchmark", sizeof(rs->name));
	strncpy(rs->unit, "ns/access", sizeof(rs->unit));

	srand(42); /* deterministic for reproducibility */

	printf("  Running cache benchmark (%llu iterations per test)...\n",
	       (unsigned long long)iterations);

	printf("\n  %-20s  %14s  %14s  %10s\n",
	       "Working Set", "Sequential", "Random", "Ratio");
	printf("  ");
	for (i = 0; i < 62; i++) putchar('-');
	printf("\n");

	for (i = 0; i < NUM_CACHE_TESTS; i++) {
		const struct cache_test_config *tc = &cache_tests[i];
		struct bench_result *r = &rs->results[rs->num_results];
		size_t size = tc->size_kb * 1024;
		size_t num_elements;
		double seq_lat, rnd_lat;
		volatile char *seq_buf;
		size_t *rnd_buf;
		uint64_t start_time;

		/* Allocate buffers */
		seq_buf = mmap(NULL, size, PROT_READ | PROT_WRITE,
			       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (seq_buf == MAP_FAILED) {
			printf("    [skip] %s (mmap failed)\n", tc->label);
			continue;
		}
		memset((void *)seq_buf, 0xBB, size);

		num_elements = size / sizeof(size_t);
		rnd_buf = mmap(NULL, size, PROT_READ | PROT_WRITE,
			       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (rnd_buf == MAP_FAILED) {
			munmap((void *)seq_buf, size);
			printf("    [skip] %s (mmap failed)\n", tc->label);
			continue;
		}

		/* Build random pointer chain */
		build_random_chain(rnd_buf, num_elements);

		if (use_procfs)
			procfs_take_snapshot(getpid(), &snap_before);

		start_time = time_ns();

		/* Run sequential benchmark */
		seq_lat = bench_sequential(seq_buf, size, iterations);

		/* Run random benchmark */
		rnd_lat = bench_random(rnd_buf, num_elements, iterations);

		double duration = (double)(time_ns() - start_time) / 1e9;

		/* Store random latency as the primary result (more interesting) */
		snprintf(r->label, sizeof(r->label), "%s", tc->label);
		r->latency_ns = rnd_lat;
		r->throughput = 1e9 / rnd_lat;
		r->iterations = iterations;
		r->duration_sec = duration;
		rs->num_results++;

		printf("  %-20s  %11.1f ns  %11.1f ns  %9.1fx\n",
		       tc->label, seq_lat, rnd_lat,
		       (seq_lat > 0) ? rnd_lat / seq_lat : 0);

		if (use_procfs) {
			procfs_take_snapshot(getpid(), &snap_after);
			procfs_diff_snapshots(&snap_before, &snap_after,
					      &rs->procfs_diff);
			rs->has_procfs = 1;
		}

		munmap((void *)seq_buf, size);
		munmap(rnd_buf, size);
	}

	printf("\n");
	return 0;
}
