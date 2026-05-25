# kprof-bench — Benchmark Suite

Набор бенчмарков для TLB, кэша и аллокатора с интеграцией procfs-профилировщика.

## Компоненты

| Файл | Описание |
|------|----------|
| `bench_main.c` | Entry point, CLI парсинг, координация бенчмарков |
| `bench_tlb.c` | TLB miss benchmark (stride access patterns) |
| `bench_cache.c` | Cache line benchmark (sequential vs random) |
| `bench_alloc.c` | Allocator benchmark (malloc/free patterns) |
| `procfs_reader.c` | Парсер `/proc/kprof/*` и `/proc/[pid]/status` |
| `procfs_reader.h` | API для чтения procfs |
| `report.c` | Форматирование результатов (text table / JSON) |
| `report.h` | API отчётов |

## Бенчмарки

### TLB Benchmark (`--tlb`)

Измеряет влияние TLB misses на производительность:
- Stride access по массиву с шагом 4KB, 2MB, 1GB
- Сравнивает latency при разных размерах рабочего набора
- Читает page faults из `/proc/kprof/pagefaults`

### Cache Benchmark (`--cache`)

Измеряет латентность доступа к памяти на разных уровнях кэша:
- Sequential vs random access patterns
- Размеры рабочего набора: 32KB (L1), 256KB (L2), 8MB (L3), 64MB+ (RAM)
- Использует `clock_gettime(CLOCK_MONOTONIC)` для точных измерений

### Allocator Benchmark (`--alloc`)

Бенчмарк аллокатора с профилировкой через procfs:
- **Паттерны**: sequential, random size, mixed alloc/free, realloc-heavy
- **Метрики**: throughput (ops/sec), latency (ns/op)
- **Procfs данные**: brk/mmap/munmap syscalls, page faults, VmRSS
- **Сравнение**: custom allocator vs glibc (с `--compare-glibc`)

## Использование

```bash
# Все бенчмарки
./kprof-bench --all

# Только TLB
./kprof-bench --tlb

# Только cache
./kprof-bench --cache

# Аллокатор с разными размерами
./kprof-bench --alloc --sizes "16,64,256,1024,4096"

# Аллокатор с JSON выводом
./kprof-bench --alloc --format json > results.json

# С чтением procfs (требуется загруженный kprof.ko)
./kprof-bench --alloc --procfs
```

## Пример вывода

```
╔══════════════════════════════════════════════════════════╗
║              kprof Allocator Benchmark Report            ║
╠══════════════════════════════════════════════════════════╣
║ Pattern: sequential malloc/free, sizes 16-4096 bytes     ║
║ Iterations: 1,000,000                                    ║
╠═══════════════════╦═══════════════╦══════════════════════╣
║ Metric            ║ myalloc       ║ glibc malloc         ║
╠═══════════════════╬═══════════════╬══════════════════════╣
║ Throughput        ║ 2.1M ops/s    ║ 8.4M ops/s           ║
║ brk() syscalls    ║ 847           ║ 12                   ║
║ mmap() syscalls   ║ 3             ║ 41                   ║
║ Minor page faults ║ 1,203         ║ 892                  ║
║ Major page faults ║ 0             ║ 0                    ║
║ Peak RSS          ║ 14.2 MB       ║ 11.8 MB              ║
║ Fragmentation     ║ 23.4%         ║ N/A                  ║
╚═══════════════════╩═══════════════╩══════════════════════╝
```

## Сборка

```bash
make                    # собирает kprof-bench
make clean              # очистка
```

## Зависимости

- `kprof-trace` (kernel module) — для `--procfs` режима
- `kprof-alloc` (libkprofalloc.so) — для бенчмарка кастомного аллокатора
