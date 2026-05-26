#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#define PAGE_SIZE 4096
// Выделяем 256 МБ памяти (65536 страниц) - это гарантированно переполнит TLB
#define NUM_PAGES 65536 

int main(void) {
    printf("=== Простой генератор TLB Misses ===\n");
    printf("Мой PID: %d\n", getpid());
    
    // Выделяем память
    size_t size = (size_t)NUM_PAGES * PAGE_SIZE;
    char *memory = (char *)malloc(size);
    if (!memory) {
        perror("Ошибка выделения памяти");
        return EXIT_FAILURE;
    }

    printf("\n1. Откройте второй терминал.\n");
    printf("2. Запустите: ./user_tracker %d\n", getpid());
    printf("3. Нажмите ENTER в этом окне, чтобы начать нагрузку...\n");
    getchar(); 

    printf("Начинаем генерацию промахов TLB! (Нажмите Ctrl+C для остановки)\n");

    // Заполняем массив нулями (ядро физически выделит память только при первой записи)
    for (size_t i = 0; i < size; i += PAGE_SIZE) {
        memory[i] = 1;
    }

    // Бесконечный цикл нагрузки на TLB
    volatile long dummy = 0; // volatile, чтобы умный компилятор не удалил цикл
    
    // Переменная для псевдослучайных прыжков
    size_t index = 0;
    // Большое простое число для прыжков (портит жизнь аппаратному prefetcher'у)
    size_t step = 1009 * PAGE_SIZE; 

    while (1) {
        // Читаем байтик памяти
        dummy += memory[index];
        
        // Прыгаем далеко вперед (сдвиг по страницам)
        index = (index + step) % size;
    }

    free(memory);
    return EXIT_SUCCESS;
}
