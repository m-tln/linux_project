# kprof-alloc — Userspace Memory Allocator

Учебная реализация `malloc()`/`free()`/`realloc()`/`calloc()` в виде shared library.

## Компоненты

| Файл | Описание |
|------|----------|
| `myalloc.c` | Основная реализация malloc/free/realloc/calloc |
| `myalloc.h` | Публичный API |
| `myalloc_internal.h` | Внутренние структуры (block header, free list) |
| `myalloc_stats.c` | Внутренняя статистика (alloc count, fragmentation) |

## Алгоритм

**First-fit** с implicit free list:

```
┌──────────┬──────────────┬──────────┬──────────────┬──────────┬─────┐
│ header_1 │   data_1     │ header_2 │   data_2     │ header_3 │ ... │
│ (used)   │   (32 bytes) │ (free)   │   (64 bytes) │ (used)   │     │
└──────────┴──────────────┴──────────┴──────────────┴──────────┴─────┘
```

**Стратегия выделения**:
- Аллокации < 128KB → `sbrk()` (расширение heap)
- Аллокации ≥ 128KB → `mmap()` (анонимный маппинг, освобождается через `munmap()`)

**Оптимизации**:
- Coalescing: слияние соседних свободных блоков при `free()`
- Block splitting: разделение большого свободного блока при `malloc()`
- Alignment: выравнивание по 16 байт

## Использование

### Как shared library (LD_PRELOAD)

```bash
# Сборка
make

# Подмена системного malloc
LD_PRELOAD=./libkprofalloc.so ./my_program

# Пример с ls
LD_PRELOAD=./libkprofalloc.so ls -la
```

### Прямое использование API

```c
#include "myalloc.h"

void *ptr = my_malloc(1024);
ptr = my_realloc(ptr, 2048);
my_free(ptr);

// Статистика
my_alloc_print_stats();
```

## Сборка

```bash
make                    # собирает libkprofalloc.so
make test               # запускает базовые тесты
make clean              # очистка
```

## Внутренняя статистика

Аллокатор ведёт внутреннюю статистику, доступную через `my_alloc_print_stats()`:

```
=== kprof-alloc stats ===
Total allocations:   15,234
Total frees:         15,100
Current allocated:   134 blocks, 2.1 MB
Peak allocated:      3.8 MB
sbrk() calls:       47
mmap() calls:       3
munmap() calls:     2
Coalescing events:   892
Fragmentation:       18.3%
```
