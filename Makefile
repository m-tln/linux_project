# Top-level Makefile for kprof project
# Usage:
#   make all        — build everything
#   make alloc      — build allocator only
#   make bench      — build benchmark only
#   make orch       — build orchestrator only
#   make trace      — build kernel module (requires linux-headers)
#   make clean      — clean all
#   make install    — install kprof CLI to /usr/local/bin
#   make load       — load kernel module
#   make unload     — unload kernel module

.PHONY: all alloc bench orch trace clean install load unload

all: alloc bench orch trace

alloc:
	$(MAKE) -C kprof-alloc

bench:
	$(MAKE) -C kprof-bench

orch:
	$(MAKE) -C kprof

trace:
	$(MAKE) -C kprof-trace

clean:
	$(MAKE) -C kprof-alloc clean 2>/dev/null || true
	$(MAKE) -C kprof-bench clean 2>/dev/null || true
	$(MAKE) -C kprof clean 2>/dev/null || true
	$(MAKE) -C kprof-trace clean 2>/dev/null || true

install: all
	sudo cp kprof/kprof /usr/local/bin/kprof
	sudo cp kprof-bench/kprof-bench /usr/local/bin/kprof-bench
	sudo cp kprof-alloc/libkprofalloc.so /usr/local/lib/
	sudo ldconfig

load:
	sudo insmod kprof-trace/kprof.ko

unload:
	sudo rmmod kprof
