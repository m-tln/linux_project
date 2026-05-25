# Makefile for kprof — Orchestrator CLI
#
# Usage:
#   make              — build kprof
#   make clean        — clean build artifacts

CC      ?= gcc
CFLAGS  := -Wall -Wextra -O2 -g -std=gnu11
LDFLAGS :=

TARGET  := kprof
SRCS    := kprof.c runner.c
OBJS    := $(SRCS:.c=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Dependencies
kprof.o: kprof.c runner.h
runner.o: runner.c runner.h

clean:
	rm -f $(TARGET) $(OBJS)
