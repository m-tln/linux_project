/*
 * runner.h — Process runner API for kprof orchestrator
 *
 * Provides functions to:
 *   - Load/unload kernel module (insmod/rmmod)
 *   - Send commands to /proc/kprof/control
 *   - Run programs with optional LD_PRELOAD
 *   - Run kprof-bench with arguments
 */

#ifndef RUNNER_H
#define RUNNER_H

#include <sys/types.h>

/*
 * Paths to kprof components (relative to kprof binary location).
 * These are resolved at runtime based on argv[0].
 */
struct kprof_paths {
	char module_path[512];    /* path to kprof.ko */
	char alloc_path[512];     /* path to libkprofalloc.so */
	char bench_path[512];     /* path to kprof-bench */
	char base_dir[512];       /* project base directory */
};

/*
 * Resolve paths to all kprof components based on the orchestrator's location.
 * Returns 0 on success, -1 on error.
 */
int runner_resolve_paths(const char *argv0, struct kprof_paths *paths);

/*
 * Check if the kernel module is currently loaded.
 * Returns 1 if loaded, 0 if not.
 */
int runner_module_loaded(void);

/*
 * Load the kernel module (sudo insmod).
 * Returns 0 on success, -1 on error.
 */
int runner_load_module(const struct kprof_paths *paths);

/*
 * Unload the kernel module (sudo rmmod).
 * Returns 0 on success, -1 on error.
 */
int runner_unload_module(void);

/*
 * Send a command to /proc/kprof/control.
 * cmd: "start <PID>", "stop", "reset"
 * Returns 0 on success, -1 on error.
 */
int runner_send_control(const char *cmd);

/*
 * Run a command and wait for it to finish.
 * Returns the exit status, or -1 on error.
 */
int runner_exec_wait(const char *const argv[]);

/*
 * Run kprof-bench with the given arguments.
 * Extra args are appended to the kprof-bench command.
 * Returns exit status or -1 on error.
 */
int runner_run_bench(const struct kprof_paths *paths,
		     const char *const extra_args[], int num_args);

/*
 * Run kprof-bench with LD_PRELOAD=libkprofalloc.so.
 * Returns exit status or -1 on error.
 */
int runner_run_bench_with_alloc(const struct kprof_paths *paths,
				const char *const extra_args[], int num_args);

/*
 * Run an arbitrary program with optional LD_PRELOAD and tracing.
 * If use_alloc is true, sets LD_PRELOAD to libkprofalloc.so.
 * If trace_pid is non-NULL, stores the child PID there.
 * Returns exit status or -1 on error.
 */
int runner_run_program(const struct kprof_paths *paths,
		       const char *const argv[],
		       int use_alloc, pid_t *trace_pid);

/*
 * Print the current status from /proc/kprof/config and summary stats.
 */
void runner_print_status(void);

#endif /* RUNNER_H */
