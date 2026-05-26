#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define PROC_FILE "/proc/tlb_monitor"

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Использование: %s <PID>\n", argv[0]);
        return EXIT_FAILURE;
    }

    // 1. Загружаем нужный PID в ядро
    FILE *proc_w = fopen(PROC_FILE, "w");
    if (!proc_w) {
        perror("Ошибка: модуль ядра не загружен (нет /proc/tlb_monitor)");
        return EXIT_FAILURE;
    }
    fprintf(proc_w, "%s\n", argv[1]);
    fclose(proc_w);

    printf("Успешно проброшен PID: %s в ядро. Начинаем мониторинг (Ctrl+C для выхода)...\n", argv[1]);
    printf("-----------------------------------------------------------\n");

    unsigned long long last_misses = 0;
    while(1) {
        // 2. Читаем данные из ядра
        FILE *proc_r = fopen(PROC_FILE, "r");
        if (!proc_r) break;

        int pid;
        unsigned long long misses = 0;
        
        // Разбираем строку формата "PID MISSES" (её мы возвращаем в tlb_show)
        if (fscanf(proc_r, "%d %llu", &pid, &misses) == 2) {
            if (pid != -1) {
                unsigned long long diff = (last_misses == 0) ? 0 : (misses - last_misses);
                last_misses = misses;

                printf("[Модуль Ядра] PID: %d | DTLB Misses: %llu (+%llu)\n", pid, misses, diff);
            }
        }
        fclose(proc_r);

        // Ждем секунду (простейший цикл без заморочек)
        sleep(1);
    }

    return EXIT_SUCCESS;
}
