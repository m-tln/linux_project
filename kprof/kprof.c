/*
 * kprof.c — Orchestrator CLI for kprof project
 *
 * Commands:
 *   kprof load                          Load kernel module
 *   kprof unload                        Unload kernel module
 *   kprof status                        Show current tracing status
 *   kprof trace --pid <PID>             Trace a running process
 *   kprof trace --exec "cmd args"       Run and trace a program
 *   kprof bench --all                   Run all benchmarks
 *   kprof bench --alloc --compare-glibc Compare allocators
 *   kprof bench --tlb --cache           TLB and cache benchmarks
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>

#include "runner.h"

static struct kprof_paths g_paths;

static void print_usage(void)
{
	fprintf(stderr,
		"Usage: kprof <command> [options]\n"
		"\n"
		"Commands:\n"
		"  load                       Load kprof kernel module\n"
		"  unload                     Unload kprof kernel module\n"
		"  status                     Show current tracing status\n"
		"  trace [options]            Trace a process\n"
		"  bench [options]            Run benchmarks\n"
		"\n"
		"Trace options:\n"
		"  --pid <PID>                Trace an existing process\n"
		"  --exec <cmd> [args...]     Run and trace a program\n"
		"  --use-alloc                Use custom allocator (LD_PRELOAD)\n"
		"\n"
		"Bench options:\n"
		"  --all                      Run all benchmarks\n"
		"  --tlb                      TLB benchmark\n"
		"  --cache                    Cache benchmark\n"
		"  --alloc                    Allocator benchmark\n"
		"  --compare-glibc            Compare custom alloc vs glibc\n"
		"  --procfs                   Collect procfs data\n"
		"  --format <text|json>       Output format\n"
		"  --iters <N>                Iterations per test\n"
		"  --sizes <s1,s2,...>         Allocation sizes\n"
		"\n"
		"Examples:\n"
		"  kprof bench --all --compare-glibc\n"
		"  kprof trace --exec ./my_program --use-alloc\n"
		"  kprof status\n"
		"\n");
}

/* ========================================================================
 * Command: load
 * ======================================================================== */

static int cmd_load(void)
{
	return runner_load_module(&g_paths);
}

/* ========================================================================
 * Command: unload
 * ======================================================================== */

static int cmd_unload(void)
{
	/* Stop tracing first */
	if (runner_module_loaded())
		runner_send_control("stop");

	return runner_unload_module();
}

/* ========================================================================
 * Command: status
 * ======================================================================== */

static int cmd_status(void)
{
	runner_print_status();
	return 0;
}

/* ========================================================================
 * Command: trace
 * ======================================================================== */

static int cmd_trace(int argc, char *argv[])
{
	pid_t target_pid = 0;
	const char *exec_cmd = NULL;
	int use_alloc = 0;
	int i;
	int exec_arg_start = -1;

	/* Parse trace options */
	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--pid") == 0 && i + 1 < argc) {
			target_pid = (pid_t)atoi(argv[++i]);
		} else if (strcmp(argv[i], "--exec") == 0 && i + 1 < argc) {
			exec_cmd = argv[++i];
			exec_arg_start = i;
		} else if (strcmp(argv[i], "--use-alloc") == 0) {
			use_alloc = 1;
		}
	}

	if (target_pid == 0 && exec_cmd == NULL) {
		fprintf(stderr, "kprof trace: specify --pid <PID> or --exec <cmd>\n");
		return 1;
	}

	/* Ensure module is loaded */
	if (!runner_module_loaded()) {
		printf("  Module not loaded, loading...\n");
		if (runner_load_module(&g_paths) != 0)
			return 1;
	}

	if (exec_cmd) {
		/* Run program and trace it */
		pid_t child_pid;

		/* Build argv for the child */
		const char *child_argv[64];
		int child_argc = 0;

		for (i = exec_arg_start; i < argc; i++) {
			if (strcmp(argv[i], "--use-alloc") == 0)
				continue;
			child_argv[child_argc++] = argv[i];
			if (child_argc >= 62) break;
		}
		child_argv[child_argc] = NULL;

		printf("\n  Starting trace for: %s\n", exec_cmd);

		/* We need to start tracing before exec.
		 * Strategy: fork, get child PID, start tracing, then let child run.
		 * Simplified: start tracing for our PID, then fork+exec.
		 */
		char cmd_buf[64];
		pid_t our_pid = getpid();
		snprintf(cmd_buf, sizeof(cmd_buf), "start %d", (int)our_pid);
		runner_send_control(cmd_buf);

		int ret = runner_run_program(&g_paths, child_argv, use_alloc,
					     &child_pid);

		/* Update tracing to child PID (retroactive — captures syscalls
		 * from fork onwards since we traced our PID) */
		printf("  Child PID was: %d, exit status: %d\n",
		       (int)child_pid, ret);

		/* Show results */
		runner_send_control("stop");
		runner_print_status();

		return ret;

	} else {
		/* Trace existing PID */
		char cmd_buf[64];
		snprintf(cmd_buf, sizeof(cmd_buf), "start %d", (int)target_pid);

		printf("  Starting trace for PID %d\n", (int)target_pid);
		printf("  Press Ctrl+C to stop tracing...\n\n");

		runner_send_control(cmd_buf);

		/* Wait for Ctrl+C */
		sigset_t mask;
		int sig;
		sigemptyset(&mask);
		sigaddset(&mask, SIGINT);
		sigprocmask(SIG_BLOCK, &mask, NULL);
		sigwait(&mask, &sig);

		printf("\n  Stopping trace...\n");
		runner_send_control("stop");
		runner_print_status();

		return 0;
	}
}

/* ========================================================================
 * Command: bench
 * ======================================================================== */

static int cmd_bench(int argc, char *argv[])
{
	int compare_glibc = 0;
	int do_alloc = 0;
	int use_procfs = 0;
	int i;

	/* Check for --compare-glibc flag */
	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--compare-glibc") == 0)
			compare_glibc = 1;
		if (strcmp(argv[i], "--alloc") == 0)
			do_alloc = 1;
		if (strcmp(argv[i], "--procfs") == 0)
			use_procfs = 1;
		if (strcmp(argv[i], "--all") == 0)
			do_alloc = 1;
	}

	/* If using procfs, ensure module is loaded */
	if (use_procfs || compare_glibc) {
		if (!runner_module_loaded()) {
			printf("  Loading kernel module for procfs data...\n");
			if (runner_load_module(&g_paths) != 0) {
				fprintf(stderr, "  Warning: module load failed, "
					"continuing without procfs\n");
			}
		}

		if (runner_module_loaded()) {
			char cmd_buf[64];

			/*
			 * Exclude the orchestrator's PID from tracing
			 * to prevent the observer effect: our procfs reads
			 * and control writes would otherwise be counted.
			 */
			snprintf(cmd_buf, sizeof(cmd_buf), "exclude %d",
				 (int)getpid());
			runner_send_control(cmd_buf);

			snprintf(cmd_buf, sizeof(cmd_buf), "start %d",
				 (int)getpid());
			runner_send_control(cmd_buf);
		}
	}

	if (compare_glibc && do_alloc) {
		/* Special mode: run allocator bench twice, compare results */
		printf("\n");
		printf("========================================\n");
		printf("  Phase 1: Benchmark with custom allocator\n");
		printf("========================================\n\n");

		/* Build args without --compare-glibc */
		const char *bench_args[32];
		int bench_argc = 0;

		for (i = 0; i < argc && bench_argc < 30; i++) {
			if (strcmp(argv[i], "--compare-glibc") == 0)
				continue;
			bench_args[bench_argc++] = argv[i];
		}

		/* Add --procfs if module is loaded */
		if (runner_module_loaded() && !use_procfs)
			bench_args[bench_argc++] = "--procfs";

		/* Reset counters */
		if (runner_module_loaded())
			runner_send_control("reset");

		/* Run with custom allocator */
		runner_run_bench_with_alloc(&g_paths, bench_args, bench_argc);

		printf("\n");
		printf("========================================\n");
		printf("  Phase 2: Benchmark with glibc malloc\n");
		printf("========================================\n\n");

		/* Reset counters */
		if (runner_module_loaded())
			runner_send_control("reset");

		/* Run with glibc */
		runner_run_bench(&g_paths, bench_args, bench_argc);

		printf("\n");
		printf("========================================\n");
		printf("  Compare the results above.\n");
		printf("  Phase 1 = custom allocator (kprof-alloc)\n");
		printf("  Phase 2 = glibc malloc\n");
		printf("========================================\n\n");

	} else {
		/* Normal mode: just run bench with given args */
		const char *bench_args[32];
		int bench_argc = 0;

		for (i = 0; i < argc && bench_argc < 30; i++)
			bench_args[bench_argc++] = argv[i];

		runner_run_bench(&g_paths, bench_args, bench_argc);
	}

	/* Cleanup */
	if (runner_module_loaded()) {
		runner_send_control("stop");
	}

	return 0;
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(int argc, char *argv[])
{
	if (argc < 2) {
		print_usage();
		return 1;
	}

	/* Resolve component paths */
	runner_resolve_paths(argv[0], &g_paths);

	const char *cmd = argv[1];

	if (strcmp(cmd, "load") == 0) {
		return cmd_load();

	} else if (strcmp(cmd, "unload") == 0) {
		return cmd_unload();

	} else if (strcmp(cmd, "status") == 0) {
		return cmd_status();

	} else if (strcmp(cmd, "trace") == 0) {
		return cmd_trace(argc - 2, argv + 2);

	} else if (strcmp(cmd, "bench") == 0) {
		return cmd_bench(argc - 2, argv + 2);

	} else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0 ||
		   strcmp(cmd, "-h") == 0) {
		print_usage();
		return 0;

	} else {
		fprintf(stderr, "kprof: unknown command '%s'\n\n", cmd);
		print_usage();
		return 1;
	}
}
