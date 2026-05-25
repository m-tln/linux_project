# Makefile for kprof-bench — Benchmark Suite
#
# Usage:
#   make              — build kprof-bench
#   make clean        — clean build artifacts
#   make run          — build and run all benchmarks

CC      ?= gcc
CFLAGS  := -Wall -Wextra -O2 -g -std=gnu11
LDFLAGS := -lrt

TARGET  := kprof-bench
SRCS    := bench_main.c bench_tlb.c bench_cache.c bench_alloc.c \
           procfs_reader.c report.c
OBJS    := $(SRCS:.c=.o)

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Dependencies
bench_main.o: bench_main.c report.h procfs_reader.h
bench_tlb.o: bench_tlb.c report.h procfs_reader.h
bench_cache.o: bench_cache.c report.h procfs_reader.h
bench_alloc.o: bench_alloc.c report.h procfs_reader.h
procfs_reader.o: procfs_reader.c procfs_reader.h
report.o: report.c report.h procfs_reader.h

clean:
	rm -f $(TARGET) $(OBJS)

run: $(TARGET)
	./$(TARGET) --all
