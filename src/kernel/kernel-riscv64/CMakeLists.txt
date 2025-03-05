cmake_minimum_required(VERSION 3.28)
set(CMAKE_CXX_STANDARD 23)

project(kernel-riscv64 VERSION 0.0.1 LANGUAGES CXX ASM)

# ------------------ Kernel Common ------------------
file(GLOB_RECURSE KERNEL_RISCV64_SRC "src/*.cpp" "src/*.asm")

add_executable(kernel-riscv64 ${KERNEL_RISCV64_SRC})

target_include_directories(kernel-riscv64 PUBLIC src)
set_target_properties(kernel-riscv64 PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})