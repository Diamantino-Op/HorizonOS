cmake_minimum_required(VERSION ${HORIZON_CMAKE_VERSION})
set(CMAKE_CXX_STANDARD ${HORIZON_CXX_STANDARD})
set(CMAKE_C_STANDARD ${HORIZON_C_STANDARD})

project(kernel VERSION ${HORIZON_VERSION} LANGUAGES CXX C ASM)

file(GLOB_RECURSE KERNEL_RISCV64_SRC "src/*.hpp" "src/*.cpp" "src/*.asm" "src/*.s")

add_executable(kernel ${KERNEL_RISCV64_SRC})

target_link_options(kernel PUBLIC ${KERNEL_LINK_FLAGS})
target_compile_options(kernel PUBLIC $<$<COMPILE_LANGUAGE:C>:${KERNEL_C_FLAGS}>)
target_compile_options(kernel PUBLIC $<$<COMPILE_LANGUAGE:CXX>:${KERNEL_CXX_FLAGS}>)
target_compile_options(kernel PUBLIC $<$<COMPILE_LANGUAGE:ASM>:${KERNEL_ASM_FLAGS}>)

target_include_directories(kernel PUBLIC src ${LIMINE_DIR} ${FLANTERM_DIR} ${CHDRS_INC_DIR} ${CXXHDRS_INC_DIR} ${NANOPRINTF_DIR})

set_target_properties(kernel PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})

target_link_libraries(kernel PUBLIC kernel-common)