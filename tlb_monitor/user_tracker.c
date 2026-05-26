#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PROC_FILE "/proc/tlb_monitor"

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "Use: %s <PID>\n", argv[0]);
		return EXIT_FAILURE;
	}

	FILE *proc_w = fopen(PROC_FILE, "w");
	if (!proc_w) {
		perror("No kernel (no /proc/tlb_monitor)");
		return EXIT_FAILURE;
	}
	fprintf(proc_w, "%s\n", argv[1]);
	fclose(proc_w);

	printf("PID: %s. Start monitoring (Ctrl+C to exit)...\n",
	       argv[1]);
	printf("-----------------------------------------------------------\n");

	unsigned long long last_misses = 0;
	while (1) {
		FILE *proc_r = fopen(PROC_FILE, "r");
		if (!proc_r)
			break;

		int pid;
		unsigned long long misses = 0;

		if (fscanf(proc_r, "%d %llu", &pid, &misses) == 2) {
			if (pid != -1) {
				unsigned long long diff =
				    (last_misses == 0) ? 0 : (misses - last_misses);
				last_misses = misses;

				printf("[Kernel] PID: %d | DTLB Misses: %llu (+%llu)\n", pid,
				       misses, diff);
			}
		}
		fclose(proc_r);

		sleep(1);
	}

	return EXIT_SUCCESS;
}
