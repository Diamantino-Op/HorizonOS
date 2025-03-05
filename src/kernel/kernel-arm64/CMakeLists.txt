cmake_minimum_required(VERSION 3.28)
set(CMAKE_CXX_STANDARD 23)

project(kernel-arm64 VERSION 0.0.1 LANGUAGES CXX ASM)

# ------------------ Kernel Common ------------------
file(GLOB_RECURSE KERNEL_ARM64_SRC "src/*.cpp" "src/*.asm")

add_executable(kernel-arm64 ${KERNEL_ARM64_SRC})

target_include_directories(kernel-arm64 PUBLIC src)
set_target_properties(kernel-arm64 PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})