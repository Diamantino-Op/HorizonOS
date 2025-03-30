cmake_minimum_required(VERSION ${HORIZON_CMAKE_VERSION})
set(CMAKE_CXX_STANDARD ${HORIZON_CXX_STANDARD})
set(CMAKE_C_STANDARD ${HORIZON_C_STANDARD})

project(kernel VERSION ${HORIZON_VERSION} LANGUAGES CXX C ASM)

file(GLOB_RECURSE KERNEL_AARCH64_SRC "src/*.hpp" "src/*.cpp" "src/*.asm" "src/*.s")

add_executable(kernel ${KERNEL_AARCH64_SRC})

target_include_directories(kernel PUBLIC src ${LIMINE_DIR} ${FLANTERM_DIR})

set_target_properties(kernel PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})

target_link_libraries(kernel kernel-common)