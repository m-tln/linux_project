/*
 * myalloc_internal.h — Internal structures for kprof allocator
 *
 * Block layout in memory:
 *
 *   +------------------+-------------------+
 *   | block_header     | user data         |
 *   | (sizeof header)  | (size bytes)      |
 *   +------------------+-------------------+
 *   ^                  ^
 *   |                  |
 *   block ptr          returned to user (malloc return value)
 *
 * Free list: singly-linked list of free blocks, traversed for first-fit.
 */

#ifndef MYALLOC_INTERNAL_H
#define MYALLOC_INTERNAL_H

#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>

/*
 * Alignment: all allocations are aligned to 16 bytes.
 * This satisfies x86_64 ABI requirements (SSE needs 16-byte alignment).
 */
#define ALLOC_ALIGNMENT     16

/*
 * Minimum block size: don't split blocks smaller than this.
 * Must be at least sizeof(block_header) + ALLOC_ALIGNMENT.
 */
#define MIN_BLOCK_SIZE      32

/*
 * Threshold for using mmap instead of sbrk.
 * Allocations >= this size use mmap (like glibc's M_MMAP_THRESHOLD).
 */
#define MMAP_THRESHOLD      (128 * 1024)  /* 128 KB */

/*
 * sbrk growth increment: when we need more heap, grow by at least this much.
 * Reduces the number of sbrk() syscalls.
 */
#define SBRK_INCREMENT      (128 * 1024)  /* 128 KB */

/*
 * Magic number for detecting corruption / double-free.
 */
#define BLOCK_MAGIC_USED    0xA110C001UL
#define BLOCK_MAGIC_FREE    0xF4EE0001UL
#define BLOCK_MAGIC_MMAP    0x004A0001UL

/*
 * Block header — prepended to every allocation.
 *
 * For sbrk-allocated blocks: linked in a free list.
 * For mmap-allocated blocks: standalone, freed with munmap().
 */
struct block_header {
	size_t               size;       /* usable size (excluding header) */
	int                  is_free;    /* 1 = free, 0 = in use */
	int                  is_mmap;    /* 1 = allocated via mmap */
	struct block_header *next;       /* next block in free list (sbrk only) */
};

/*
 * Ensure header size is aligned.
 */
#define HEADER_SIZE  \
	((sizeof(struct block_header) + ALLOC_ALIGNMENT - 1) & ~(ALLOC_ALIGNMENT - 1))

/*
 * Convert between block header and user pointer.
 */
#define BLOCK_TO_PTR(block)  ((void *)((char *)(block) + HEADER_SIZE))
#define PTR_TO_BLOCK(ptr)    ((struct block_header *)((char *)(ptr) - HEADER_SIZE))

/*
 * Align size up to ALLOC_ALIGNMENT.
 */
#define ALIGN_UP(size)  (((size) + ALLOC_ALIGNMENT - 1) & ~(ALLOC_ALIGNMENT - 1))

/*
 * Global state (defined in myalloc.c)
 */
extern struct block_header *free_list_head;
extern pthread_mutex_t      alloc_mutex;

/*
 * Statistics tracking (defined in myalloc_stats.c)
 */
void stats_record_alloc(size_t size);
void stats_record_free(size_t size);
void stats_record_realloc(void);
void stats_record_sbrk(void);
void stats_record_mmap(void);
void stats_record_munmap(void);
void stats_record_coalesce(void);
void stats_record_split(void);
void stats_update_heap_size(size_t heap_size);
void stats_compute_fragmentation(void);

#endif /* MYALLOC_INTERNAL_H */
