# kprof — Orchestrator CLI

Единая CLI-утилита, координирующая все компоненты проекта: kernel module, allocator и benchmark suite.

## Компоненты

| Файл | Описание |
|------|----------|
| `kprof.c` | Main CLI: парсинг команд, dispatch |
| `runner.c` | Запуск процессов: insmod/rmmod, LD_PRELOAD, fork/exec |
| `runner.h` | API runner |

## Команды

### Бенчмарки

```bash
# Полный бенчмарк: TLB + cache + allocator, сравнение с glibc
kprof bench --all --compare-glibc

# Только TLB и cache
kprof bench --tlb --cache

# Только аллокатор с кастомными размерами
kprof bench --alloc --sizes "16,64,256,1024,4096"
```

### Профилировка процесса

```bash
# Профилировка по PID
kprof trace --pid 1234

# Запуск программы с профилировкой
kprof trace --exec "./my_program arg1 arg2"

# Запуск с кастомным аллокатором + профилировка
kprof trace --exec "./my_program" --use-alloc
```

### Управление модулем

```bash
kprof load          # sudo insmod kprof.ko
kprof unload        # sudo rmmod kprof
kprof status        # cat /proc/kprof/config + /proc/kprof/syscalls
```

## Workflow: полный бенчмарк

Что делает `kprof bench --all --compare-glibc`:

```
1. sudo insmod kprof.ko                              # загрузка модуля
2. echo "start $$" > /proc/kprof/control              # начало трейсинга
3. LD_PRELOAD=libkprofalloc.so kprof-bench --alloc     # бенчмарк с myalloc
4. → сохраняет results_custom.json
5. echo "reset" > /proc/kprof/control                  # сброс счётчиков
6. kprof-bench --alloc                                 # бенчмарк с glibc
7. → сохраняет results_glibc.json
8. kprof-bench --tlb --cache                           # TLB/cache бенчмарки
9. echo "stop" > /proc/kprof/control                   # остановка трейсинга
10. sudo rmmod kprof                                   # выгрузка модуля
11. → вывод сравнительной таблицы
```

## Сборка

```bash
make                    # собирает kprof
make clean              # очистка
```

## Зависимости

- `kprof-trace/kprof.ko` — kernel module
- `kprof-alloc/libkprofalloc.so` — shared library аллокатора
- `kprof-bench/kprof-bench` — бенчмарк бинарник
