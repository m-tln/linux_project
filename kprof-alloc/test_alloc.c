/*
 * test_alloc.c — Basic tests for kprof custom allocator
 *
 * Build: make test
 * Run:   ./test_alloc
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "myalloc.h"

#define TEST(name) \
	do { printf("  [TEST] %-40s ", name); } while (0)
#define PASS() \
	do { printf("PASS\n"); passed++; } while (0)
#define FAIL(msg) \
	do { printf("FAIL: %s\n", msg); failed++; } while (0)

static int passed = 0;
static int failed = 0;

static void test_basic_malloc_free(void)
{
	TEST("basic malloc/free");

	void *p = my_malloc(100);
	if (!p) { FAIL("malloc returned NULL"); return; }

	memset(p, 0xAA, 100);
	my_free(p);

	PASS();
}

static void test_zero_malloc(void)
{
	TEST("malloc(0) returns NULL");

	void *p = my_malloc(0);
	if (p != NULL) { FAIL("expected NULL"); my_free(p); return; }

	PASS();
}

static void test_free_null(void)
{
	TEST("free(NULL) is safe");

	my_free(NULL); /* should not crash */

	PASS();
}

static void test_multiple_allocs(void)
{
	TEST("multiple allocations");

	void *ptrs[100];
	int i;

	for (i = 0; i < 100; i++) {
		ptrs[i] = my_malloc(64);
		if (!ptrs[i]) { FAIL("malloc returned NULL"); return; }
		memset(ptrs[i], (char)i, 64);
	}

	/* Verify data integrity */
	for (i = 0; i < 100; i++) {
		unsigned char *p = (unsigned char *)ptrs[i];
		if (p[0] != (unsigned char)i || p[63] != (unsigned char)i) {
			FAIL("data corruption");
			return;
		}
	}

	for (i = 0; i < 100; i++)
		my_free(ptrs[i]);

	PASS();
}

static void test_realloc_grow(void)
{
	TEST("realloc grow");

	void *p = my_malloc(32);
	if (!p) { FAIL("malloc returned NULL"); return; }

	memset(p, 0xBB, 32);

	p = my_realloc(p, 256);
	if (!p) { FAIL("realloc returned NULL"); return; }

	/* First 32 bytes should be preserved */
	unsigned char *bp = (unsigned char *)p;
	if (bp[0] != 0xBB || bp[31] != 0xBB) {
		FAIL("data not preserved after realloc");
		my_free(p);
		return;
	}

	my_free(p);
	PASS();
}

static void test_realloc_null(void)
{
	TEST("realloc(NULL, size) == malloc(size)");

	void *p = my_realloc(NULL, 128);
	if (!p) { FAIL("returned NULL"); return; }

	my_free(p);
	PASS();
}

static void test_realloc_zero(void)
{
	TEST("realloc(ptr, 0) == free(ptr)");

	void *p = my_malloc(64);
	if (!p) { FAIL("malloc returned NULL"); return; }

	void *r = my_realloc(p, 0);
	if (r != NULL) { FAIL("expected NULL"); return; }

	PASS();
}

static void test_calloc(void)
{
	TEST("calloc zeroes memory");

	int *p = (int *)my_calloc(100, sizeof(int));
	if (!p) { FAIL("calloc returned NULL"); return; }

	int i;
	for (i = 0; i < 100; i++) {
		if (p[i] != 0) {
			FAIL("memory not zeroed");
			my_free(p);
			return;
		}
	}

	my_free(p);
	PASS();
}

static void test_large_alloc(void)
{
	TEST("large allocation (mmap path)");

	/* Allocate > MMAP_THRESHOLD (128KB) */
	size_t size = 256 * 1024;
	void *p = my_malloc(size);
	if (!p) { FAIL("malloc returned NULL"); return; }

	memset(p, 0xCC, size);
	my_free(p);

	PASS();
}

static void test_mixed_sizes(void)
{
	TEST("mixed allocation sizes");

	void *p1 = my_malloc(16);
	void *p2 = my_malloc(1024);
	void *p3 = my_malloc(64);
	void *p4 = my_malloc(4096);
	void *p5 = my_malloc(8);

	if (!p1 || !p2 || !p3 || !p4 || !p5) {
		FAIL("malloc returned NULL");
		return;
	}

	/* Free in non-sequential order */
	my_free(p3);
	my_free(p1);
	my_free(p5);
	my_free(p2);
	my_free(p4);

	PASS();
}

static void test_reuse_freed_blocks(void)
{
	TEST("reuse freed blocks");

	/* Allocate and free to create free blocks */
	void *p1 = my_malloc(64);
	void *p2 = my_malloc(64);
	my_free(p1);

	/* This should reuse p1's block */
	void *p3 = my_malloc(64);
	if (!p3) { FAIL("malloc returned NULL"); return; }

	my_free(p2);
	my_free(p3);

	PASS();
}

static void test_stats(void)
{
	TEST("statistics tracking");

	my_alloc_reset_stats();

	void *p1 = my_malloc(100);
	void *p2 = my_malloc(200);
	my_free(p1);

	struct myalloc_stats s;
	my_alloc_get_stats(&s);

	if (s.total_allocs < 2) { FAIL("alloc count wrong"); my_free(p2); return; }
	if (s.total_frees < 1) { FAIL("free count wrong"); my_free(p2); return; }
	if (s.current_blocks < 1) { FAIL("current blocks wrong"); my_free(p2); return; }

	my_free(p2);
	PASS();
}

static void test_stress(void)
{
	TEST("stress test (10000 alloc/free)");

	int i;
	void *ptrs[1000];

	for (i = 0; i < 10; i++) {
		int j;
		/* Allocate batch */
		for (j = 0; j < 1000; j++) {
			size_t sz = 16 + (j % 512) * 8;
			ptrs[j] = my_malloc(sz);
			if (!ptrs[j]) { FAIL("malloc returned NULL"); return; }
			((char *)ptrs[j])[0] = (char)j;
		}
		/* Free batch in reverse */
		for (j = 999; j >= 0; j--) {
			my_free(ptrs[j]);
		}
	}

	PASS();
}

int main(void)
{
	printf("\n=== kprof-alloc test suite ===\n\n");

	test_basic_malloc_free();
	test_zero_malloc();
	test_free_null();
	test_multiple_allocs();
	test_realloc_grow();
	test_realloc_null();
	test_realloc_zero();
	test_calloc();
	test_large_alloc();
	test_mixed_sizes();
	test_reuse_freed_blocks();
	test_stats();
	test_stress();

	printf("\n=== Results: %d passed, %d failed ===\n\n", passed, failed);

	if (failed > 0) {
		my_alloc_print_stats();
		return 1;
	}

	my_alloc_print_stats();
	return 0;
}
