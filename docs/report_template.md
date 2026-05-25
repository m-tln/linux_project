# Отчёт: kprof — Educational Linux Profiler & Allocator Benchmark Suite

## 1. Введение

**Цель проекта**: разработка учебного профилировщика Linux, объединяющего syscall/page fault трейсинг, userspace аллокатор и benchmark suite.

**Объединённые темы**:
- Procfs/System Monitor
- Syscall Tracer
- Page Fault Tracer
- TLB/Cache Benchmark Suite
- Userspace malloc()/allocator

## 2. Архитектура

_(Вставить диаграмму из docs/architecture.md)_

### 2.1 kprof-trace (Kernel Module)
- Механизм: kprobes
- Интерфейс: /proc/kprof/*
- Объём кода: ___ строк C

### 2.2 kprof-alloc (Userspace Allocator)
- Алгоритм: first-fit
- Стратегия: sbrk + mmap
- Объём кода: ___ строк C

### 2.3 kprof-bench (Benchmark Suite)
- Бенчмарки: TLB, cache, allocator
- Интеграция с procfs
- Объём кода: ___ строк C

### 2.4 kprof (Orchestrator)
- CLI координация
- Объём кода: ___ строк C

## 3. Реализация

### 3.1 Syscall Tracing

_(Описание реализации kprobes, per-CPU счётчиков, seq_file)_

### 3.2 Page Fault Tracing

_(Описание kprobe на handle_mm_fault, atomic counters)_

### 3.3 Procfs Interface

_(Описание proc_create, seq_file API, control interface)_

### 3.4 Allocator

_(Описание first-fit, block header, coalescing, sbrk/mmap)_

## 4. Результаты бенчмарков

### 4.1 TLB Benchmark

| Stride | Throughput (MB/s) | Page Faults |
|--------|-------------------|-------------|
| 4 KB   |                   |             |
| 2 MB   |                   |             |
| 1 GB   |                   |             |

### 4.2 Cache Benchmark

| Working Set | Sequential (ns) | Random (ns) | Ratio |
|-------------|-----------------|-------------|-------|
| 32 KB (L1)  |                 |             |       |
| 256 KB (L2) |                 |             |       |
| 8 MB (L3)   |                 |             |       |
| 64 MB (RAM) |                 |             |       |

### 4.3 Allocator Comparison

| Metric            | myalloc | glibc malloc |
|-------------------|---------|--------------|
| Throughput (ops/s) |         |              |
| brk() syscalls    |         |              |
| mmap() syscalls   |         |              |
| Minor page faults |         |              |
| Peak RSS (MB)     |         |              |
| Fragmentation (%) |         |              |

## 5. Анализ результатов

_(Объяснение почему myalloc медленнее/быстрее glibc, анализ syscall overhead, page fault patterns)_

## 6. Возможные улучшения

- [ ] SMP-safe implementation (per-CPU free lists)
- [ ] Segregated free lists вместо first-fit
- [ ] mmap support в procfs
- [ ] JSON export для визуализации
- [ ] eBPF вместо kprobes

## 7. Заключение

_(Итоги, что было изучено, практическая ценность)_
