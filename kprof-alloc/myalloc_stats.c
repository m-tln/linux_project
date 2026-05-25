/*
 * myalloc_stats.c — Internal statistics tracking for kprof allocator
 *
 * All stats functions are called with alloc_mutex held (from myalloc.c),
 * so no additional locking is needed here.
 */

#include <stdio.h>
#include <string.h>

#include "myalloc.h"
#include "myalloc_internal.h"

/* Global statistics */
static struct myalloc_stats g_stats = {0};

/* ========================================================================
 * Recording functions (called from myalloc.c)
 * ======================================================================== */

void stats_record_alloc(size_t size)
{
	g_stats.total_allocs++;
	g_stats.current_blocks++;
	g_stats.current_bytes += size;

	if (g_stats.current_bytes > g_stats.peak_bytes)
		g_stats.peak_bytes = g_stats.current_bytes;
}

void stats_record_free(size_t size)
{
	g_stats.total_frees++;
	if (g_stats.current_blocks > 0)
		g_stats.current_blocks--;
	if (g_stats.current_bytes >= size)
		g_stats.current_bytes -= size;
}

void stats_record_realloc(void)
{
	g_stats.total_reallocs++;
}

void stats_record_sbrk(void)
{
	g_stats.sbrk_calls++;
}

void stats_record_mmap(void)
{
	g_stats.mmap_calls++;
}

void stats_record_munmap(void)
{
	g_stats.munmap_calls++;
}

void stats_record_coalesce(void)
{
	g_stats.coalesce_count++;
}

void stats_record_split(void)
{
	g_stats.split_count++;
}

void stats_update_heap_size(size_t heap_size)
{
	g_stats.heap_size = heap_size;
}

/*
 * Compute internal fragmentation.
 *
 * Fragmentation = (total free space in free list) / (total heap size)
 * This measures how much of the heap is wasted in free blocks.
 */
void stats_compute_fragmentation(void)
{
	struct block_header *curr;
	size_t free_bytes = 0;
	size_t total_bytes = 0;

	curr = free_list_head;
	while (curr) {
		total_bytes += curr->size;
		if (curr->is_free)
			free_bytes += curr->size;
		curr = curr->next;
	}

	if (total_bytes > 0)
		g_stats.fragmentation = (double)free_bytes / (double)total_bytes * 100.0;
	else
		g_stats.fragmentation = 0.0;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

void my_alloc_get_stats(struct myalloc_stats *stats)
{
	pthread_mutex_lock(&alloc_mutex);
	stats_compute_fragmentation();
	memcpy(stats, &g_stats, sizeof(*stats));
	pthread_mutex_unlock(&alloc_mutex);
}

void my_alloc_print_stats(void)
{
	struct myalloc_stats s;

	my_alloc_get_stats(&s);

	fprintf(stderr,
		"\n"
		"=== kprof-alloc statistics ===\n"
		"Total allocations:   %lu\n"
		"Total frees:         %lu\n"
		"Total reallocs:      %lu\n"
		"Current blocks:      %lu\n"
		"Current allocated:   %zu bytes (%.2f KB)\n"
		"Peak allocated:      %zu bytes (%.2f KB)\n"
		"Heap size (sbrk):    %zu bytes (%.2f KB)\n"
		"sbrk() calls:        %lu\n"
		"mmap() calls:        %lu\n"
		"munmap() calls:      %lu\n"
		"Coalescing events:   %lu\n"
		"Block splits:        %lu\n"
		"Fragmentation:       %.1f%%\n"
		"==============================\n\n",
		s.total_allocs,
		s.total_frees,
		s.total_reallocs,
		s.current_blocks,
		s.current_bytes, (double)s.current_bytes / 1024.0,
		s.peak_bytes, (double)s.peak_bytes / 1024.0,
		s.heap_size, (double)s.heap_size / 1024.0,
		s.sbrk_calls,
		s.mmap_calls,
		s.munmap_calls,
		s.coalesce_count,
		s.split_count,
		s.fragmentation);
}

void my_alloc_reset_stats(void)
{
	pthread_mutex_lock(&alloc_mutex);
	memset(&g_stats, 0, sizeof(g_stats));
	pthread_mutex_unlock(&alloc_mutex);
}
