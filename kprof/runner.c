/*
 * runner.c — Process runner implementation for kprof orchestrator
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <libgen.h>
#include <limits.h>

#include "runner.h"

#define PROC_KPROF_CONTROL  "/proc/kprof/control"
#define PROC_KPROF_CONFIG   "/proc/kprof/config"
#define PROC_KPROF_SYSCALLS "/proc/kprof/syscalls"
#define PROC_MODULES        "/proc/modules"

/*
 * Resolve paths to all kprof components.
 *
 * Layout expected:
 *   <base>/kprof/kprof          <- orchestrator binary
 *   <base>/kprof-trace/kprof.ko
 *   <base>/kprof-alloc/libkprofalloc.so
 *   <base>/kprof-bench/kprof-bench
 */
int runner_resolve_paths(const char *argv0, struct kprof_paths *paths)
{
	char resolved[PATH_MAX];
	char *dir;
	char tmp[PATH_MAX];

	memset(paths, 0, sizeof(*paths));

	/* Resolve the real path of the binary */
	if (realpath(argv0, resolved) == NULL) {
		/* Fallback: use argv0 as-is */
		strncpy(resolved, argv0, sizeof(resolved) - 1);
	}

	/* Get directory of the binary */
	strncpy(tmp, resolved, sizeof(tmp) - 1);
	dir = dirname(tmp);

	/* Base dir is parent of kprof/ */
	snprintf(paths->base_dir, sizeof(paths->base_dir), "%s/..", dir);

	/* Resolve base_dir to absolute */
	if (realpath(paths->base_dir, resolved) != NULL)
		strncpy(paths->base_dir, resolved, sizeof(paths->base_dir) - 1);

	snprintf(paths->module_path, sizeof(paths->module_path),
		 "%s/kprof-trace/kprof.ko", paths->base_dir);
	snprintf(paths->alloc_path, sizeof(paths->alloc_path),
		 "%s/kprof-alloc/libkprofalloc.so", paths->base_dir);
	snprintf(paths->bench_path, sizeof(paths->bench_path),
		 "%s/kprof-bench/kprof-bench", paths->base_dir);

	return 0;
}

/*
 * Check if kprof module is loaded by scanning /proc/modules.
 */
int runner_module_loaded(void)
{
	FILE *fp;
	char line[256];

	fp = fopen(PROC_MODULES, "r");
	if (!fp)
		return 0;

	while (fgets(line, sizeof(line), fp)) {
		if (strncmp(line, "kprof ", 6) == 0) {
			fclose(fp);
			return 1;
		}
	}

	fclose(fp);
	return 0;
}

/*
 * Execute a command (fork + exec) and wait for completion.
 */
int runner_exec_wait(const char *const argv[])
{
	pid_t pid;
	int status;

	pid = fork();
	if (pid < 0) {
		fprintf(stderr, "kprof: fork failed: %s\n", strerror(errno));
		return -1;
	}

	if (pid == 0) {
		/* Child */
		execvp(argv[0], (char *const *)argv);
		fprintf(stderr, "kprof: exec '%s' failed: %s\n",
			argv[0], strerror(errno));
		_exit(127);
	}

	/* Parent: wait for child */
	if (waitpid(pid, &status, 0) < 0) {
		fprintf(stderr, "kprof: waitpid failed: %s\n", strerror(errno));
		return -1;
	}

	if (WIFEXITED(status))
		return WEXITSTATUS(status);

	return -1;
}

/*
 * Load kernel module.
 */
int runner_load_module(const struct kprof_paths *paths)
{
	struct stat st;

	if (runner_module_loaded()) {
		printf("  [info] kprof module already loaded\n");
		return 0;
	}

	/* Check if module file exists */
	if (stat(paths->module_path, &st) != 0) {
		fprintf(stderr, "kprof: module not found at %s\n",
			paths->module_path);
		fprintf(stderr, "  Run 'make -C kprof-trace' first\n");
		return -1;
	}

	printf("  Loading kernel module: %s\n", paths->module_path);

	const char *argv[] = {"sudo", "insmod", paths->module_path, NULL};
	int ret = runner_exec_wait(argv);

	if (ret != 0) {
		fprintf(stderr, "kprof: insmod failed (exit %d)\n", ret);
		return -1;
	}

	printf("  [ok] kprof module loaded\n");
	return 0;
}

/*
 * Unload kernel module.
 */
int runner_unload_module(void)
{
	if (!runner_module_loaded()) {
		printf("  [info] kprof module not loaded\n");
		return 0;
	}

	printf("  Unloading kernel module...\n");

	const char *argv[] = {"sudo", "rmmod", "kprof", NULL};
	int ret = runner_exec_wait(argv);

	if (ret != 0) {
		fprintf(stderr, "kprof: rmmod failed (exit %d)\n", ret);
		return -1;
	}

	printf("  [ok] kprof module unloaded\n");
	return 0;
}

/*
 * Send command to /proc/kprof/control.
 */
int runner_send_control(const char *cmd)
{
	FILE *fp;

	fp = fopen(PROC_KPROF_CONTROL, "w");
	if (!fp) {
		fprintf(stderr, "kprof: cannot open %s: %s\n",
			PROC_KPROF_CONTROL, strerror(errno));
		fprintf(stderr, "  Is the kprof module loaded? Try: kprof load\n");
		return -1;
	}

	fprintf(fp, "%s\n", cmd);
	fclose(fp);
	return 0;
}

/*
 * Run kprof-bench with extra arguments.
 */
int runner_run_bench(const struct kprof_paths *paths,
		     const char *const extra_args[], int num_args)
{
	const char *argv[32];
	int argc = 0;
	int i;
	struct stat st;

	if (stat(paths->bench_path, &st) != 0) {
		fprintf(stderr, "kprof: bench binary not found at %s\n",
			paths->bench_path);
		fprintf(stderr, "  Run 'make -C kprof-bench' first\n");
		return -1;
	}

	argv[argc++] = paths->bench_path;
	for (i = 0; i < num_args && argc < 30; i++)
		argv[argc++] = extra_args[i];
	argv[argc] = NULL;

	return runner_exec_wait(argv);
}

/*
 * Run kprof-bench with LD_PRELOAD=libkprofalloc.so.
 */
int runner_run_bench_with_alloc(const struct kprof_paths *paths,
				const char *const extra_args[], int num_args)
{
	pid_t pid;
	int status;
	int i;
	struct stat st;

	if (stat(paths->alloc_path, &st) != 0) {
		fprintf(stderr, "kprof: allocator not found at %s\n",
			paths->alloc_path);
		fprintf(stderr, "  Run 'make -C kprof-alloc' first\n");
		return -1;
	}

	if (stat(paths->bench_path, &st) != 0) {
		fprintf(stderr, "kprof: bench binary not found at %s\n",
			paths->bench_path);
		return -1;
	}

	pid = fork();
	if (pid < 0) {
		fprintf(stderr, "kprof: fork failed: %s\n", strerror(errno));
		return -1;
	}

	if (pid == 0) {
		/* Child: set LD_PRELOAD and exec bench */
		setenv("LD_PRELOAD", paths->alloc_path, 1);

		const char *argv[32];
		int argc = 0;

		argv[argc++] = paths->bench_path;
		for (i = 0; i < num_args && argc < 30; i++)
			argv[argc++] = extra_args[i];
		argv[argc] = NULL;

		execv(paths->bench_path, (char *const *)argv);
		fprintf(stderr, "kprof: exec failed: %s\n", strerror(errno));
		_exit(127);
	}

	if (waitpid(pid, &status, 0) < 0)
		return -1;

	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/*
 * Run an arbitrary program with optional LD_PRELOAD.
 */
int runner_run_program(const struct kprof_paths *paths,
		       const char *const argv[],
		       int use_alloc, pid_t *trace_pid)
{
	pid_t pid;
	int status;

	pid = fork();
	if (pid < 0) {
		fprintf(stderr, "kprof: fork failed: %s\n", strerror(errno));
		return -1;
	}

	if (pid == 0) {
		/* Child */
		if (use_alloc)
			setenv("LD_PRELOAD", paths->alloc_path, 1);

		execvp(argv[0], (char *const *)argv);
		fprintf(stderr, "kprof: exec '%s' failed: %s\n",
			argv[0], strerror(errno));
		_exit(127);
	}

	/* Parent */
	if (trace_pid)
		*trace_pid = pid;

	if (waitpid(pid, &status, 0) < 0)
		return -1;

	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/*
 * Print current kprof status.
 */
void runner_print_status(void)
{
	FILE *fp;
	char line[256];

	printf("\n=== kprof status ===\n\n");

	/* Module status */
	if (runner_module_loaded()) {
		printf("  Module: loaded\n\n");
	} else {
		printf("  Module: not loaded\n");
		printf("  (use 'kprof load' to load the module)\n\n");
		return;
	}

	/* Config */
	fp = fopen(PROC_KPROF_CONFIG, "r");
	if (fp) {
		printf("  Config:\n");
		while (fgets(line, sizeof(line), fp))
			printf("    %s", line);
		printf("\n");
		fclose(fp);
	}

	/* Syscall summary (first 10 lines) */
	fp = fopen(PROC_KPROF_SYSCALLS, "r");
	if (fp) {
		int count = 0;

		printf("  Syscalls (top entries):\n");
		while (fgets(line, sizeof(line), fp) && count < 12) {
			printf("    %s", line);
			count++;
		}
		if (fgets(line, sizeof(line), fp))
			printf("    ... (more entries, see /proc/kprof/syscalls)\n");
		printf("\n");
		fclose(fp);
	}

	printf("====================\n\n");
}
