/*
 * myalloc.h — Public API for kprof custom allocator
 *
 * This allocator can be used in two ways:
 *
 * 1. Direct API: call my_malloc(), my_free(), etc.
 *    #include "myalloc.h"
 *    void *p = my_malloc(1024);
 *    my_free(p);
 *
 * 2. LD_PRELOAD: replaces system malloc/free transparently
 *    LD_PRELOAD=./libkprofalloc.so ./my_program
 */

#ifndef MYALLOC_H
#define MYALLOC_H

#include <stddef.h>

/*
 * Core allocation functions (my_* prefix).
 * These are always available regardless of LD_PRELOAD.
 */
void *my_malloc(size_t size);
void  my_free(void *ptr);
void *my_realloc(void *ptr, size_t size);
void *my_calloc(size_t nmemb, size_t size);

/*
 * Statistics API
 */

struct myalloc_stats {
	unsigned long total_allocs;      /* total malloc() calls */
	unsigned long total_frees;       /* total free() calls */
	unsigned long total_reallocs;    /* total realloc() calls */
	unsigned long current_blocks;    /* currently allocated blocks */
	size_t        current_bytes;     /* currently allocated bytes */
	size_t        peak_bytes;        /* peak allocated bytes */
	size_t        heap_size;         /* total heap size (sbrk) */
	unsigned long sbrk_calls;        /* number of sbrk() calls */
	unsigned long mmap_calls;        /* number of mmap() calls */
	unsigned long munmap_calls;      /* number of munmap() calls */
	unsigned long coalesce_count;    /* number of coalescing events */
	unsigned long split_count;       /* number of block splits */
	double        fragmentation;     /* internal fragmentation % */
};

/*
 * Get current allocator statistics.
 */
void my_alloc_get_stats(struct myalloc_stats *stats);

/*
 * Print statistics to stderr.
 */
void my_alloc_print_stats(void);

/*
 * Reset statistics counters (does not affect allocations).
 */
void my_alloc_reset_stats(void);

#endif /* MYALLOC_H */
