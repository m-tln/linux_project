# kprof — Educational Linux Profiler & Allocator Benchmark Suite

Учебный профилировщик Linux, объединяющий syscall/page fault трейсинг, userspace аллокатор и benchmark suite.

## Объединённые темы курса

| Тема | Компонент |
|------|-----------|
| Procfs/System Monitor | `kprof-trace` — `/proc/kprof/*` интерфейс |
| Syscall Tracer | `kprof-trace` — kprobes на sys_enter/sys_exit |
| Page Fault Tracer | `kprof-trace` — kprobe на handle_mm_fault |
| TLB/Cache Benchmark Suite | `kprof-bench` — stride/random access бенчмарки |
| Userspace malloc()/allocator | `kprof-alloc` — first-fit malloc/free |

## Архитектура

```
┌─────────────────────────────────────────────────────┐
│              kprof (orchestrator CLI)                │
└──────┬──────────────────┬──────────────────┬────────┘
       │                  │                  │
       ▼                  ▼                  ▼
┌──────────────┐  ┌───────────────┐  ┌───────────────┐
│ kprof-trace  │  │ kprof-alloc   │  │ kprof-bench   │
│ kernel module│  │ userspace lib │  │ benchmark     │
│ /proc/kprof/*│  │ malloc/free   │  │ TLB/cache/    │
│ syscall trace│  │ sbrk + mmap   │  │ allocator     │
│ pagefault    │  │               │  │ procfs reader │
└──────────────┘  └───────────────┘  └───────────────┘
```

Подробная архитектура: [docs/architecture.md](docs/architecture.md)

## Структура проекта

```
├── kprof-trace/        # Kernel module (syscall + pagefault tracer + procfs)
├── kprof-alloc/        # Userspace allocator (libkprofalloc.so)
├── kprof-bench/        # Benchmark suite (TLB, cache, allocator)
├── kprof/              # Orchestrator CLI
├── scripts/            # Setup и demo скрипты
├── docs/               # Архитектура и шаблон отчёта
└── Makefile            # Top-level build
```

## Quick Start

### 1. Сборка

```bash
# Всё (требуется linux-headers для kernel module)
make all

# Только userspace компоненты
make alloc bench orch
```

### 2. Запуск бенчмарка

```bash
# Полный бенчмарк с загрузкой модуля и сравнением аллокаторов
sudo ./kprof/kprof bench --all --compare-glibc

# Или вручную:
sudo insmod kprof-trace/kprof.ko
echo "start $$" > /proc/kprof/control
LD_PRELOAD=./kprof-alloc/libkprofalloc.so ./kprof-bench/kprof-bench --alloc
cat /proc/kprof/syscalls
sudo rmmod kprof
```

### 3. Профилировка программы

```bash
sudo ./kprof/kprof trace --exec "./my_program"
```

## Требования

- Linux kernel 6.x с `CONFIG_KPROBES=y`
- `linux-headers-$(uname -r)`
- GCC, Make
- Рекомендуется: VM (QEMU/VirtualBox) для разработки kernel module

## Документация

- [Архитектура](docs/architecture.md)
- [Шаблон отчёта](docs/report_template.md)
- [kprof-trace README](kprof-trace/README.md)
- [kprof-alloc README](kprof-alloc/README.md)
- [kprof-bench README](kprof-bench/README.md)
- [kprof orchestrator README](kprof/README.md)

## Демо

```bash
chmod +x scripts/demo.sh
sudo ./scripts/demo.sh
```

## Лицензия

Educational project — курс Linux Kernel.
