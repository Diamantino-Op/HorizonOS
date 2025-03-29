cmake_minimum_required(VERSION 3.28)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_STANDARD 23)

project(kernel-common VERSION 0.0.1 LANGUAGES CXX ASM)

# ------------------ Kernel Common ------------------
file(GLOB_RECURSE KERNEL_COMMON_SRC "src/*.hpp" "src/*.cpp" "src/*.asm" "src/*.s")
file(GLOB_RECURSE FLANTERM_SRC "${FLANTERM_DIR}/*.h" "${FLANTERM_DIR}/*.c")

add_library(kernel-common STATIC ${KERNEL_COMMON_SRC} ${FLANTERM_SRC})

target_include_directories(kernel-common PUBLIC src)