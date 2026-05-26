cmake_minimum_required(VERSION 3.15)
project(TLB_Benchmark_Suite C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(user_tracker user_tracker.c)
target_compile_options(user_tracker PRIVATE -Wall -Wextra -O2)

add_executable(benchmark benchmark.c)
target_compile_options(benchmark PRIVATE -Wall -Wextra -O0)

set(KDIR "/lib/modules/${CMAKE_SYSTEM_VERSION}/build" CACHE PATH "Kernel build path")

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/tlb_module.ko
    COMMAND make -C ${KDIR} M=${CMAKE_CURRENT_SOURCE_DIR} modules
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tlb_module.c
    COMMENT "Building Kernel Module via Kbuild..."
)

add_custom_target(tlb_module ALL
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tlb_module.ko
)

add_custom_target(kernel_clean
    COMMAND make -C ${KDIR} M=${CMAKE_CURRENT_SOURCE_DIR} clean
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Cleaning Kernel Module via Kbuild..."
)
# Привязываем очистку ядра к общей команде clean
set_property(GLOBAL APPEND PROPERTY ADDITIONAL_MAKE_CLEAN_FILES kernel_clean)

# Get this message cmake -B build -G Ninja
message(STATUS "==========================================================")
message(STATUS "[TLB Benchmark Suite]")
message(STATUS "")
message(STATUS "  1. Compile:         ninja -C build")
message(STATUS "  2. Load kernel:     sudo insmod tlb_module.ko")
message(STATUS "  3. Start benchmark: ./build/benchmark")
message(STATUS "  4. Start client:    ./build/user_tracker <PID>")
message(STATUS "  5. Unload kernel:   sudo rmmod tlb_module")
message(STATUS "  6. Clean build:     ninja -C build clean && make -C /lib/modules/$(uname -r)/build M=$(pwd) clean")
message(STATUS "==========================================================")
