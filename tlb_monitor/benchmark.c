#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define BATCH_SIZE 50000000ULL

volatile sig_atomic_t keep_running = 1;

void sigint_handler(int dummy)
{
	(void)dummy;
	keep_running = 0;
}

typedef struct {
	size_t size_mb;
	size_t page_size;
	size_t stride_pages;
	int use_chasing;
	int disable_thp;
	size_t restrict_bytes;
} BenchConfig;

void apply_profile(const char *profile_name, BenchConfig *cfg)
{
	if (strcmp(profile_name, "linear") == 0) {
		cfg->size_mb = 16;
		cfg->stride_pages = 0;
		cfg->use_chasing = 0;
		cfg->disable_thp = 0;
	} else if (strcmp(profile_name, "stride") == 0) {
		cfg->size_mb = 64;
		cfg->stride_pages = 1009;
		cfg->use_chasing = 0;
		cfg->disable_thp = 1;
	} else if (strcmp(profile_name, "ultimate") == 0) {
		cfg->size_mb = 256;
		cfg->use_chasing = 1;
		cfg->disable_thp = 1;
	} else if (strcmp(profile_name, "zero") == 0) {
		cfg->size_mb = 1;
		cfg->stride_pages = 0;
		cfg->use_chasing = 0;
		cfg->disable_thp = 0;
		cfg->restrict_bytes = 4096;
	} else {
		fprintf(stderr, "Неизвестный профиль: %s. Возврат к дефолту.\n", profile_name);
	}
}

void parse_args(int argc, char **argv, BenchConfig *cfg)
{
	struct option long_options[] = {{"profile", required_argument, 0, 'p'},
					{"mem", required_argument, 0, 'm'},
					{"page", required_argument, 0, 'P'},
					{"stride", required_argument, 0, 's'},
					{"chasing", no_argument, 0, 'c'},
					{"no-thp", no_argument, 0, 'N'},
					{0, 0, 0, 0}};
	int opt;
	while ((opt = getopt_long(argc, argv, "p:m:P:s:cN", long_options, NULL)) != -1) {
		switch (opt) {
		case 'p':
			apply_profile(optarg, cfg);
			break;
		case 'm':
			cfg->size_mb = (size_t)atoi(optarg);
			break;
		case 'P':
			cfg->page_size = (size_t)atoi(optarg);
			break;
		case 's':
			cfg->stride_pages = (size_t)atoi(optarg);
			break;
		case 'c':
			cfg->use_chasing = 1;
			break;
		case 'N':
			cfg->disable_thp = 1;
			break;
		default:
			exit(EXIT_FAILURE);
		}
	}
}

/* --- ФУНКЦИИ РАБОТЫ С ПАМЯТЬЮ --- */

char *prepare_memory(const BenchConfig *cfg)
{
	size_t total_bytes = cfg->size_mb * 1024 * 1024;
	char *memory = (char *)malloc(total_bytes);
	if (!memory) {
		perror("malloc failed");
		exit(EXIT_FAILURE);
	}

	if (cfg->disable_thp) {
		if (madvise(memory, total_bytes, MADV_NOHUGEPAGE) != 0) {
			perror("madvise (MADV_NOHUGEPAGE) failed. Keep going");
		}
	}

	for (size_t i = 0; i < total_bytes; i += cfg->page_size) {
		memory[i] = 1;
	}
	return memory;
}

size_t build_chasing_list(char *memory, const BenchConfig *cfg)
{
	size_t total_pages = (cfg->size_mb * 1024 * 1024) / cfg->page_size;
	size_t *indices = malloc(total_pages * sizeof(size_t));
	if (!indices)
		exit(EXIT_FAILURE);

	for (size_t i = 0; i < total_pages; i++)
		indices[i] = i;

	srand((unsigned int)time(NULL));
	for (size_t i = total_pages - 1; i > 0; i--) {
		size_t j = rand() % (i + 1);
		size_t temp = indices[i];
		indices[i] = indices[j];
		indices[j] = temp;
	}

	for (size_t i = 0; i < total_pages; i++) {
		size_t curr_off = indices[i] * cfg->page_size;
		size_t next_off = indices[(i + 1) % total_pages] * cfg->page_size;
		*((size_t *)(memory + curr_off)) = next_off;
	}

	size_t start_offset = indices[0] * cfg->page_size;
	free(indices);
	return start_offset;
}

void run_benchmark(char *memory, size_t start_offset, const BenchConfig *cfg)
{
	size_t total_bytes = cfg->size_mb * 1024 * 1024;
	struct timespec start, end;

	printf("\n=== Start (batch size %lld ops) ===\n", BATCH_SIZE);

	if (cfg->use_chasing) {
		register size_t offset = start_offset;
		while (keep_running) {
			clock_gettime(CLOCK_MONOTONIC, &start);

			for (size_t i = 0; i < BATCH_SIZE; i++) {
				offset = *((volatile size_t *)(memory + offset));
			}

			clock_gettime(CLOCK_MONOTONIC, &end);
			double time_ns =
			    (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
			printf("Time to access: %.2f ns/op\n", time_ns / BATCH_SIZE);
		}
	} else {
		volatile long dummy = 0;
		register size_t index = 0;
		register size_t step_bytes =
		    (cfg->stride_pages == 0) ? 1 : (cfg->stride_pages * cfg->page_size);

		const size_t limit = (cfg->restrict_bytes > 0) ? cfg->restrict_bytes : total_bytes;
		while (keep_running) {
			clock_gettime(CLOCK_MONOTONIC, &start);

			for (size_t i = 0; i < BATCH_SIZE; i++) {
				dummy += memory[index];
				index = (index + step_bytes) % limit;
			}

			clock_gettime(CLOCK_MONOTONIC, &end);
			double time_ns =
			    (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
			printf("Time to access: %.2f ns/op\n", time_ns / BATCH_SIZE);
		}
	}
}

/* --- IDEAL MAIN --- */

int main(int argc, char **argv)
{
	BenchConfig cfg = {.size_mb = 64,
			   .page_size = 4096,
			   .stride_pages = 1009,
			   .use_chasing = 0,
			   .disable_thp = 1,
			   .restrict_bytes = 0};
	parse_args(argc, argv, &cfg);

	printf("\n=== TLB Benchmark | PID: %d ===\n", getpid());
	printf("Config: %zu MB | %s THP | %s Chasing\n\n", cfg.size_mb,
	       cfg.disable_thp ? "Disable" : "Enable", cfg.use_chasing ? "ON" : "OFF");

	char *memory = prepare_memory(&cfg);

	size_t start_offset = 0;
	if (cfg.use_chasing) {
		start_offset = build_chasing_list(memory, &cfg);
	}

	signal(SIGINT, sigint_handler);

	printf("Tap ENTER to start...\n");
	getchar();

	run_benchmark(memory, start_offset, &cfg);

	free(memory);
	return EXIT_SUCCESS;
}
