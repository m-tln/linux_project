cmake_minimum_required(VERSION 3.15)
project(TLB_Benchmark_Suite C)

# --- 1. Настройки сборки User-Space (компилируется через Ninja) ---
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_executable(user_tracker user_tracker.c)
target_compile_options(user_tracker PRIVATE -Wall -Wextra -O2)

add_executable(benchmark benchmark.c)
target_compile_options(benchmark PRIVATE -Wall -Wextra -O0) 

# --- 2. Настройки сборки Kernel Module (строгая делегация в Kbuild) ---
# Находим папку с исходниками текущего ядра
set(KDIR "/lib/modules/${CMAKE_SYSTEM_VERSION}/build" CACHE PATH "Kernel build path")

# Говорим Ninja: "Чтобы получить .ko файл, выполни команду make -C..."
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/tlb_module.ko
    COMMAND make -C ${KDIR} M=${CMAKE_CURRENT_SOURCE_DIR} modules
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tlb_module.c
    COMMENT "Building Kernel Module via Kbuild..."
)

# Создаем цель, чтобы Ninja собирал модуль по умолчанию вместе с user_tracker
add_custom_target(tlb_module ALLIAS kernel_module)
add_custom_target(kernel_module ALL
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tlb_module.ko
)

# Очистка мусора ядра, когда мы вызываем ninja clean
set_property(DIRECTORY PROPERTY ADDITIONAL_CLEAN_FILES 
    "tlb_module.ko" "tlb_module.mod.c" "tlb_module.mod.o" 
    "tlb_module.o" "modules.order" "Module.symvers" ".tlb_module*"
)
