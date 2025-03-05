cmake_minimum_required(VERSION 3.28)
set(CMAKE_CXX_STANDARD 23)

project(kernel-common VERSION 0.0.1 LANGUAGES CXX ASM)

# ------------------ Kernel Common ------------------
file(GLOB_RECURSE KERNEL_COMMON_SRC "src/*.cpp" "src/*.asm")

add_library(kernel-common STATIC ${KERNEL_COMMON_SRC})

target_include_directories(kernel-common PUBLIC src)