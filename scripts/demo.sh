#!/bin/bash
# demo.sh — Демонстрационный скрипт для защиты проекта
#
# Использование:
#   chmod +x scripts/demo.sh
#   sudo ./scripts/demo.sh
#
# Показывает полный workflow: загрузка модуля → бенчмарк → сравнение → выгрузка

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
KPROF_MOD="$PROJECT_DIR/kprof-trace/kprof.ko"
KPROF_ALLOC="$PROJECT_DIR/kprof-alloc/libkprofalloc.so"
KPROF_BENCH="$PROJECT_DIR/kprof-bench/kprof-bench"

echo "╔══════════════════════════════════════════════════╗"
echo "║     kprof — Educational Linux Profiler Demo      ║"
echo "╚══════════════════════════════════════════════════╝"
echo ""

# Step 1: Load kernel module
echo "=== Step 1: Loading kernel module ==="
sudo insmod "$KPROF_MOD"
echo "  ✓ kprof.ko loaded"
echo ""

# Step 2: Show procfs interface
echo "=== Step 2: Procfs interface ==="
echo "  /proc/kprof/config:"
cat /proc/kprof/config 2>/dev/null || echo "  (not available yet)"
echo ""

# Step 3: Start tracing
echo "=== Step 3: Start tracing (PID $$) ==="
echo "start $$" > /proc/kprof/control
echo "  ✓ Tracing started for PID $$"
echo ""

# Step 4: Run allocator benchmark with custom allocator
echo "=== Step 4: Benchmark with custom allocator ==="
LD_PRELOAD="$KPROF_ALLOC" "$KPROF_BENCH" --alloc --procfs 2>&1 || echo "  (benchmark not built yet)"
echo ""

# Step 5: Show syscall stats
echo "=== Step 5: Syscall statistics ==="
cat /proc/kprof/syscalls 2>/dev/null || echo "  (not available)"
echo ""

# Step 6: Show page fault stats
echo "=== Step 6: Page fault statistics ==="
cat /proc/kprof/pagefaults 2>/dev/null || echo "  (not available)"
echo ""

# Step 7: Reset and run with glibc
echo "=== Step 7: Reset & benchmark with glibc malloc ==="
echo "reset" > /proc/kprof/control
"$KPROF_BENCH" --alloc --procfs 2>&1 || echo "  (benchmark not built yet)"
echo ""

# Step 8: TLB/Cache benchmark
echo "=== Step 8: TLB/Cache benchmark ==="
"$KPROF_BENCH" --tlb --cache 2>&1 || echo "  (benchmark not built yet)"
echo ""

# Step 9: Cleanup
echo "=== Step 9: Cleanup ==="
echo "stop" > /proc/kprof/control
sudo rmmod kprof
echo "  ✓ kprof.ko unloaded"
echo ""

echo "╔══════════════════════════════════════════════════╗"
echo "║                  Demo complete!                   ║"
echo "╚══════════════════════════════════════════════════╝"
