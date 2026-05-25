# Architecture — kprof: Educational Linux Profiler & Allocator Benchmark Suite

## Overview

**kprof** — учебный профилировщик Linux, объединяющий 4 темы курса Linux Kernel:

1. **Procfs/System Monitor** — экспорт данных через `/proc/kprof/*`
2. **Syscall Tracer** — перехват и подсчёт системных вызовов через kprobes
3. **Page Fault Tracer** — отслеживание page faults через kprobes
4. **TLB/Cache Benchmark Suite** — бенчмарки TLB/кэша и аллокатора

Дополнительно реализуется **userspace allocator** (`malloc`/`free`), который бенчмаркится с помощью профилировщика.

---

## High-Level Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    kprof (orchestrator)                      │
│         CLI: координирует все 3 тулзы                       │
│         insmod/rmmod, LD_PRELOAD, запуск бенчмарков         │
└──────┬──────────────────┬──────────────────┬────────────────┘
       │                  │                  │
       ▼                  ▼                  ▼
┌──────────────┐  ┌───────────────┐  ┌───────────────┐
│ kprof-trace  │  │ kprof-alloc   │  │ kprof-bench   │
│ (kernel mod) │  │ (userspace    │  │ (benchmark    │
│              │  │  allocator)   │  │  suite)       │
│ • syscall    │  │              │  │              │
│   tracer     │  │ • malloc()   │  │ • TLB bench  │
│ • pagefault  │  │ • free()     │  │ • cache bench│
│   tracer     │  │ • realloc()  │  │ • alloc bench│
│ • /proc/kprof│  │ • calloc()   │  │ • procfs     │
│   interface  │  │ • stats      │  │   reader     │
└──────────────┘  └───────────────┘  └───────────────┘
     KERNEL            USERSPACE          USERSPACE
```

---

## Component Details

### Tool 1: kprof-trace (Kernel Module)

**Назначение**: перехват syscalls и page faults для целевого процесса, экспорт статистики через procfs.

**Механизм**:
- `kprobes` на `sys_enter` / `sys_exit` tracepoints — подсчёт количества и времени каждого syscall
- `kprobe` на `handle_mm_fault()` — подсчёт minor/major page faults
- Per-CPU счётчики для lock-free сбора статистики

**Procfs интерфейс** (`/proc/kprof/`):

| Файл | Тип | Описание |
|------|-----|----------|
| `syscalls` | read-only | Таблица: syscall_nr, name, count, total_ns |
| `pagefaults` | read-only | Таблица: PID, minor_count, major_count, last_addr |
| `control` | write-only | Команды: `start <PID>`, `stop`, `reset` |
| `config` | read-only | Текущее состояние: target PID, tracing active/inactive |

**Ключевые структуры данных**:
```c
struct kprof_syscall_stat {
    unsigned long count;        // количество вызовов
    u64           total_ns;     // суммарное время (наносекунды)
};

struct kprof_state {
    pid_t                    target_pid;
    bool                     active;
    struct kprof_syscall_stat syscalls[NR_syscalls];
    atomic_long_t            minor_faults;
    atomic_long_t            major_faults;
};
```

---

### Tool 2: kprof-alloc (Userspace Allocator)

**Назначение**: реализация `malloc()`/`free()`/`realloc()`/`calloc()` в виде shared library (`libkprofalloc.so`), подключаемой через `LD_PRELOAD`.

**Алгоритм**: First-fit с linked list свободных блоков.

**Стратегия выделения памяти**:
- Аллокации < 128KB → `sbrk()` (расширение heap)
- Аллокации ≥ 128KB → `mmap()` (анонимный маппинг)

**Структура блока**:
```c
struct block_header {
    size_t size;              // размер данных (без заголовка)
    int    is_free;           // 1 = свободен, 0 = занят
    struct block_header *next; // следующий блок в списке
};
```

**Оптимизации**:
- Coalescing: слияние соседних свободных блоков при `free()`
- Alignment: выравнивание по 16 байт
- Внутренняя статистика: количество alloc/free, текущий heap size, фрагментация

---

### Tool 3: kprof-bench (Benchmark Suite)

**Назначение**: бенчмарки TLB, кэша и аллокатора с чтением данных из procfs.

**Бенчмарки**:

1. **TLB Benchmark** (`bench_tlb.c`):
   - Stride access по большому массиву с разным шагом (4KB, 2MB, 1GB)
   - Измеряет деградацию производительности при TLB miss
   - Читает page faults из `/proc/kprof/pagefaults`

2. **Cache Benchmark** (`bench_cache.c`):
   - Sequential vs random access patterns
   - Разные размеры рабочего набора (L1, L2, L3, RAM)
   - Измеряет latency через `clock_gettime(CLOCK_MONOTONIC)`

3. **Allocator Benchmark** (`bench_alloc.c`):
   - Паттерны: sequential, random size, mixed alloc/free
   - Измеряет throughput (ops/sec)
   - Читает `/proc/kprof/syscalls` до и после — считает brk/mmap/munmap
   - Читает `/proc/[pid]/status` — VmRSS, VmPeak
   - Сравнение: custom allocator vs glibc malloc

**Procfs Reader** (`procfs_reader.c`):
- Парсит `/proc/kprof/syscalls` и `/proc/kprof/pagefaults`
- Парсит `/proc/[pid]/status` (VmRSS, VmSize, voluntary_ctxt_switches)
- Парсит `/proc/[pid]/maps` (heap size, mmap regions)

---

### Orchestrator: kprof

**Назначение**: единая CLI-утилита, координирующая все 3 тулзы.

**Команды**:
```bash
kprof bench --all --compare-glibc    # Полный бенчмарк с сравнением
kprof bench --tlb --cache            # Только TLB/cache
kprof bench --alloc                  # Только аллокатор
kprof trace --pid <PID>              # Профилировка процесса
kprof trace --exec "./program"       # Запуск и профилировка
kprof status                         # Текущая статистика
kprof load                           # sudo insmod kprof.ko
kprof unload                         # sudo rmmod kprof
```

**Workflow полного бенчмарка**:
```
1. sudo insmod kprof.ko
2. echo "start $$" > /proc/kprof/control
3. LD_PRELOAD=libkprofalloc.so kprof-bench --alloc  →  results_custom.json
4. echo "reset" > /proc/kprof/control
5. kprof-bench --alloc                               →  results_glibc.json
6. echo "stop" > /proc/kprof/control
7. sudo rmmod kprof
8. Вывод сравнительной таблицы
```

---

## Полезная информация из procfs

### Per-process (`/proc/[pid]/...`)

| Файл | Что читаем | Зачем |
|------|-----------|-------|
| `status` | VmRSS, VmSize, VmPeak, VmSwap, context switches | Профилировка памяти |
| `maps` | Карта VM: heap, stack, mmap regions | Визуализация layout аллокатора |
| `smaps` | RSS, PSS, Shared/Private по регионам | Точный анализ потребления |
| `stat` | CPU time (user/system), threads, state | CPU профилировка |
| `io` | read_bytes, write_bytes, syscr, syscw | I/O профилировка |

### System-wide (`/proc/...`)

| Файл | Что читаем | Зачем |
|------|-----------|-------|
| `vmstat` | pgfault, pgmajfault, pswpin/out | Page fault статистика |
| `meminfo` | MemTotal, MemFree, Cached | Общая картина памяти |
| `stat` | CPU time по ядрам, context switches | Системная нагрузка |

---

## Project Structure

```
LinuxKernel/
├── Makefile                        # Top-level: make all / make clean
├── README.md                       # Обзор проекта
├── THEMES.md                       # Исходные темы
│
├── kprof-trace/                    # Tool 1: Kernel module
│   ├── Makefile
│   ├── kprof_main.c
│   ├── kprof_syscall.c
│   ├── kprof_pagefault.c
│   ├── kprof_proc.c
│   ├── kprof.h
│   └── README.md
│
├── kprof-alloc/                    # Tool 2: Userspace allocator
│   ├── Makefile
│   ├── myalloc.c
│   ├── myalloc.h
│   ├── myalloc_internal.h
│   ├── myalloc_stats.c
│   └── README.md
│
├── kprof-bench/                    # Tool 3: Benchmark suite
│   ├── Makefile
│   ├── bench_main.c
│   ├── bench_tlb.c
│   ├── bench_cache.c
│   ├── bench_alloc.c
│   ├── procfs_reader.c
│   ├── procfs_reader.h
│   ├── report.c
│   ├── report.h
│   └── README.md
│
├── kprof/                          # Orchestrator
│   ├── Makefile
│   ├── kprof.c
│   ├── runner.c
│   ├── runner.h
│   └── README.md
│
├── scripts/
│   ├── setup_vm.sh
│   └── demo.sh
│
└── docs/
    ├── architecture.md             # Этот файл
    └── report_template.md
```

---

## Development Environment

- **Разработка kernel module**: в виртуальной машине (QEMU + buildroot или VirtualBox + Ubuntu)
- **Разработка userspace**: на хост-машине или в VM
- **Ядро**: Linux 6.x (рекомендуется 6.1 LTS или новее)
- **Компилятор**: gcc с поддержкой `-Wall -Wextra -Werror`
- **Тестирование**: `insmod`/`rmmod`, `dmesg`, `cat /proc/kprof/*`
