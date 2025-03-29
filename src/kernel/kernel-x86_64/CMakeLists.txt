cmake_minimum_required(VERSION 3.28)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_STANDARD 23)

project(kernel VERSION 0.0.1 LANGUAGES CXX ASM)

# ------------------ Kernel Common ------------------
file(GLOB_RECURSE KERNEL_X86_64_SRC "src/*.hpp" "src/*.cpp" "src/*.asm" "src/*.s")

add_executable(kernel ${KERNEL_X86_64_SRC})

target_include_directories(kernel PUBLIC src)