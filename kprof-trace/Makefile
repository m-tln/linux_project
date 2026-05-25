# Kbuild Makefile for kprof kernel module
#
# Usage:
#   make                — build kprof.ko
#   make clean          — clean build artifacts
#   make install        — install module (requires root)
#
# Prerequisites:
#   - Linux kernel headers: apt install linux-headers-$(uname -r)
#   - Build tools: apt install build-essential

# Module name
obj-m := kprof.o

# Multi-file module: kprof.ko is built from these object files
kprof-objs := kprof_main.o kprof_syscall.o kprof_pagefault.o kprof_proc.o

# Kernel build directory
KDIR ?= /lib/modules/$(shell uname -r)/build

# Current directory
PWD := $(shell pwd)

# Default target
all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install

# Convenience targets
load:
	sudo insmod kprof.ko

unload:
	sudo rmmod kprof

reload: unload load

.PHONY: all clean install load unload reload
