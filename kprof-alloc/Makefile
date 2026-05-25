# Makefile for kprof-alloc — Custom Memory Allocator
#
# Usage:
#   make              — build libkprofalloc.so
#   make test         — build and run basic tests
#   make clean        — clean build artifacts
#
# Usage with LD_PRELOAD:
#   LD_PRELOAD=./libkprofalloc.so ./some_program

CC      ?= gcc
CFLAGS  := -Wall -Wextra -O2 -g -std=gnu11 -fPIC
LDFLAGS := -shared -lpthread

TARGET  := libkprofalloc.so
SRCS    := myalloc.c myalloc_stats.c
OBJS    := $(SRCS:.c=.o)

# Test binary
TEST_TARGET := test_alloc
TEST_SRCS   := test_alloc.c

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Dependencies
myalloc.o: myalloc.c myalloc.h myalloc_internal.h
myalloc_stats.o: myalloc_stats.c myalloc.h myalloc_internal.h

# Simple test program (links directly, not via LD_PRELOAD)
test: $(TARGET) $(TEST_TARGET)
	./$(TEST_TARGET)

$(TEST_TARGET): $(TEST_SRCS) myalloc.h
	$(CC) -Wall -Wextra -O2 -g -std=gnu11 -o $@ $(TEST_SRCS) -L. -lkprofalloc -Wl,-rpath,.

clean:
	rm -f $(TARGET) $(OBJS) $(TEST_TARGET)
