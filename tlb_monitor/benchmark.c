#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define PAGE_SIZE 4096
#define NUM_PAGES 65536 // 256 MB

volatile sig_atomic_t keep_running = 1;
void sigint_handler(int dummy)
{
	(void)dummy;
	keep_running = 0;
}

int main(void)
{
	printf("=== TLB Misses ===\n");
	printf("PID: %d\n", getpid());

	size_t size = (size_t)NUM_PAGES * PAGE_SIZE;
	char *memory = (char *)malloc(size);
	if (!memory) {
		perror("malloc failed");
		return EXIT_FAILURE;
	}

	signal(SIGINT, sigint_handler);

	// Break COW and lazy allocation
	for (size_t i = 0; i < size; i += PAGE_SIZE) {
		memory[i] = 1;
	}

	printf("\n1. Open second terminal.\n");
	printf("2. Start ./user_tracker %d\n", getpid());
	printf("3. Tap ENTER to load...\n");
	getchar();

	printf("Start TLB misses! (Ctrl+C to exit)\n");

	volatile long dummy = 0;
	size_t index = 0;
	size_t step = 1009 * PAGE_SIZE;

	while (keep_running) {

		dummy += memory[index];
		index = (index + step) % size;
	}

	free(memory);
	return EXIT_SUCCESS;
}
