# kprof-trace — Kernel Tracing Module

Модуль ядра Linux для перехвата и профилировки системных вызовов и page faults.

## Компоненты

| Файл | Описание |
|------|----------|
| `kprof_main.c` | Инициализация модуля, параметры (`target_pid`) |
| `kprof_syscall.c` | Syscall tracer через kprobes на `sys_enter`/`sys_exit` |
| `kprof_pagefault.c` | Page fault tracer через kprobe на `handle_mm_fault()` |
| `kprof_proc.c` | Procfs интерфейс (`/proc/kprof/*`) через `seq_file` API |
| `kprof.h` | Общие структуры данных и объявления |

## Procfs Interface

После загрузки модуля (`sudo insmod kprof.ko`) появляются файлы:

```bash
# Статистика syscalls (read-only)
cat /proc/kprof/syscalls
# NR    NAME            COUNT    TOTAL_NS
# 0     read            1523     4821000
# 1     write           892      2103000
# 9     mmap            41       890000
# 12    brk             12       45000

# Статистика page faults (read-only)
cat /proc/kprof/pagefaults
# PID    MINOR    MAJOR    LAST_ADDR
# 1234   1203     0        0x7f4a2c001000

# Управление (write-only)
echo "start 1234" > /proc/kprof/control   # начать трейсинг PID 1234
echo "stop"       > /proc/kprof/control   # остановить
echo "reset"      > /proc/kprof/control   # сбросить счётчики

# Текущая конфигурация (read-only)
cat /proc/kprof/config
# target_pid: 1234
# active: yes
```

## Сборка

```bash
# Требуется установленные linux-headers
make

# Загрузка
sudo insmod kprof.ko

# Выгрузка
sudo rmmod kprof

# Логи
dmesg | grep kprof
```

## Зависимости

- Linux kernel headers (`linux-headers-$(uname -r)`)
- Ядро Linux 6.x с поддержкой kprobes (`CONFIG_KPROBES=y`)

## Ключевые API ядра

- `kprobe_register()` / `kprobe_unregister()` — регистрация kprobes
- `proc_mkdir()` / `proc_create()` — создание procfs файлов
- `seq_file` API — вывод данных в procfs
- `atomic_long_t` — атомарные счётчики для lock-free статистики
