cmake_minimum_required(VERSION 3.28)
set(CMAKE_CXX_STANDARD 23)

project(kernel-x86_64 VERSION 0.0.1 LANGUAGES CXX ASM)

# ------------------ Kernel Common ------------------
file(GLOB_RECURSE KERNEL_X86_64_SRC "src/*.cpp" "src/*.asm")

add_executable(kernel-x86_64 ${KERNEL_X86_64_SRC})

target_include_directories(kernel-x86_64 PUBLIC src)
set_target_properties(kernel-x86_64 PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})