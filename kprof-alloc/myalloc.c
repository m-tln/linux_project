/*
 * myalloc.c — Custom memory allocator implementation
 *
 * Algorithm: First-fit with implicit free list
 *
 * Strategy:
 *   - Small allocations (< 128KB): sbrk() to extend heap
 *   - Large allocations (>= 128KB): mmap() anonymous pages
 *
 * Optimizations:
 *   - Block splitting: large free blocks are split when possible
 *   - Coalescing: adjacent free blocks are merged on free()
 *   - 16-byte alignment for all allocations
 *   - Minimum sbrk increment to reduce syscall overhead
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <pthread.h>

#include "myalloc.h"
#include "myalloc_internal.h"

/* Global free list head */
struct block_header *free_list_head = NULL;

/* Mutex for thread safety */
pthread_mutex_t alloc_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Track the program break for heap size calculation */
static void *heap_start = NULL;

/*
 * Find a free block using first-fit strategy.
 * Returns the first free block that is large enough, or NULL.
 */
static struct block_header *find_free_block(size_t size)
{
	struct block_header *curr = free_list_head;

	while (curr) {
		if (curr->is_free && curr->size >= size)
			return curr;
		curr = curr->next;
	}
	return NULL;
}

/*
 * Split a block if it's large enough to hold the requested size
 * plus a new block header and minimum block size.
 */
static void split_block(struct block_header *block, size_t size)
{
	size_t remaining;

	if (block->size < size + HEADER_SIZE + MIN_BLOCK_SIZE)
		return; /* not enough room to split */

	remaining = block->size - size - HEADER_SIZE;

	/* Create new free block after the allocated portion */
	struct block_header *new_block =
		(struct block_header *)((char *)block + HEADER_SIZE + size);

	new_block->size = remaining;
	new_block->is_free = 1;
	new_block->is_mmap = 0;
	new_block->next = block->next;

	block->size = size;
	block->next = new_block;

	stats_record_split();
}

/*
 * Extend the heap using sbrk().
 * Allocates at least SBRK_INCREMENT bytes to reduce syscall frequency.
 */
static struct block_header *extend_heap(size_t size)
{
	size_t total = HEADER_SIZE + size;
	size_t increment;
	struct block_header *block;
	void *ptr;

	/* Round up to SBRK_INCREMENT */
	increment = total;
	if (increment < SBRK_INCREMENT)
		increment = SBRK_INCREMENT;

	ptr = sbrk((intptr_t)increment);
	if (ptr == (void *)-1) {
		fprintf(stderr, "myalloc: sbrk(%zu) failed: %s\n",
			increment, strerror(errno));
		return NULL;
	}

	if (!heap_start)
		heap_start = ptr;

	stats_record_sbrk();

	block = (struct block_header *)ptr;
	block->size = increment - HEADER_SIZE;
	block->is_free = 0;
	block->is_mmap = 0;
	block->next = NULL;

	/* Append to free list */
	if (!free_list_head) {
		free_list_head = block;
	} else {
		struct block_header *curr = free_list_head;
		while (curr->next)
			curr = curr->next;
		curr->next = block;
	}

	/* Update heap size stat */
	stats_update_heap_size((size_t)((char *)sbrk(0) - (char *)heap_start));

	/* Split if we allocated more than needed */
	if (block->size > size + HEADER_SIZE + MIN_BLOCK_SIZE)
		split_block(block, size);

	return block;
}

/*
 * Allocate using mmap (for large allocations).
 */
static struct block_header *mmap_alloc(size_t size)
{
	size_t total = HEADER_SIZE + size;
	void *ptr;

	ptr = mmap(NULL, total, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (ptr == MAP_FAILED) {
		fprintf(stderr, "myalloc: mmap(%zu) failed: %s\n",
			total, strerror(errno));
		return NULL;
	}

	stats_record_mmap();

	struct block_header *block = (struct block_header *)ptr;
	block->size = size;
	block->is_free = 0;
	block->is_mmap = 1;
	block->next = NULL;

	return block;
}

/*
 * Coalesce adjacent free blocks.
 * Merges current block with the next block if both are free.
 */
static void coalesce(void)
{
	struct block_header *curr = free_list_head;

	while (curr && curr->next) {
		if (curr->is_free && curr->next->is_free) {
			/* Check if blocks are physically adjacent */
			char *curr_end = (char *)curr + HEADER_SIZE + curr->size;
			if (curr_end == (char *)curr->next) {
				curr->size += HEADER_SIZE + curr->next->size;
				curr->next = curr->next->next;
				stats_record_coalesce();
				continue; /* check again with merged block */
			}
		}
		curr = curr->next;
	}
}

/* ========================================================================
 * Public API: my_malloc, my_free, my_realloc, my_calloc
 * ======================================================================== */

void *my_malloc(size_t size)
{
	struct block_header *block;
	size_t aligned_size;

	if (size == 0)
		return NULL;

	aligned_size = ALIGN_UP(size);

	pthread_mutex_lock(&alloc_mutex);

	/* Large allocation: use mmap */
	if (aligned_size >= MMAP_THRESHOLD) {
		block = mmap_alloc(aligned_size);
		if (block) {
			stats_record_alloc(aligned_size);
			pthread_mutex_unlock(&alloc_mutex);
			return BLOCK_TO_PTR(block);
		}
		pthread_mutex_unlock(&alloc_mutex);
		return NULL;
	}

	/* Try to find a free block (first-fit) */
	block = find_free_block(aligned_size);

	if (block) {
		/* Found a free block — split if possible */
		split_block(block, aligned_size);
		block->is_free = 0;
	} else {
		/* No suitable block — extend heap */
		block = extend_heap(aligned_size);
		if (!block) {
			pthread_mutex_unlock(&alloc_mutex);
			return NULL;
		}
		block->is_free = 0;
	}

	stats_record_alloc(aligned_size);
	pthread_mutex_unlock(&alloc_mutex);

	return BLOCK_TO_PTR(block);
}

void my_free(void *ptr)
{
	struct block_header *block;

	if (!ptr)
		return;

	pthread_mutex_lock(&alloc_mutex);

	block = PTR_TO_BLOCK(ptr);

	if (block->is_mmap) {
		/* mmap'd block: return to OS immediately */
		size_t total = HEADER_SIZE + block->size;
		stats_record_free(block->size);
		stats_record_munmap();
		munmap(block, total);
		pthread_mutex_unlock(&alloc_mutex);
		return;
	}

	/* Mark as free */
	block->is_free = 1;
	stats_record_free(block->size);

	/* Try to coalesce with neighbors */
	coalesce();

	pthread_mutex_unlock(&alloc_mutex);
}

void *my_realloc(void *ptr, size_t size)
{
	struct block_header *block;
	void *new_ptr;
	size_t copy_size;

	if (!ptr)
		return my_malloc(size);

	if (size == 0) {
		my_free(ptr);
		return NULL;
	}

	block = PTR_TO_BLOCK(ptr);

	/* If current block is large enough, just return it */
	if (block->size >= ALIGN_UP(size))
		return ptr;

	/* Allocate new block, copy data, free old */
	new_ptr = my_malloc(size);
	if (!new_ptr)
		return NULL;

	copy_size = block->size < size ? block->size : size;
	memcpy(new_ptr, ptr, copy_size);
	my_free(ptr);

	stats_record_realloc();

	return new_ptr;
}

void *my_calloc(size_t nmemb, size_t size)
{
	size_t total;
	void *ptr;

	/* Check for overflow */
	total = nmemb * size;
	if (nmemb != 0 && total / nmemb != size)
		return NULL;

	ptr = my_malloc(total);
	if (ptr)
		memset(ptr, 0, total);

	return ptr;
}

/* ========================================================================
 * LD_PRELOAD wrappers: override system malloc/free/realloc/calloc
 *
 * When this library is loaded via LD_PRELOAD, these symbols replace
 * the glibc versions, making all allocations go through our allocator.
 * ======================================================================== */

void *malloc(size_t size)
{
	return my_malloc(size);
}

void free(void *ptr)
{
	my_free(ptr);
}

void *realloc(void *ptr, size_t size)
{
	return my_realloc(ptr, size);
}

void *calloc(size_t nmemb, size_t size)
{
	return my_calloc(nmemb, size);
}
