cmake_minimum_required(VERSION ${HORIZON_CMAKE_VERSION})
set(CMAKE_CXX_STANDARD ${HORIZON_CXX_STANDARD})
set(CMAKE_C_STANDARD ${HORIZON_C_STANDARD})

project(kernel VERSION ${HORIZON_VERSION} LANGUAGES CXX C ASM)

file(GLOB_RECURSE KERNEL_X86_64_SRC "src/*.c" "src/*.cpp" "src/*.hpp" "src/*.h" "src/*.asm" "src/*.s")

add_executable(kernel ${KERNEL_X86_64_SRC} )

target_link_options(kernel PUBLIC ${KERNEL_LINK_FLAGS})
target_compile_options(kernel PUBLIC $<$<COMPILE_LANGUAGE:C>:${KERNEL_C_FLAGS}>)
target_compile_options(kernel PUBLIC $<$<COMPILE_LANGUAGE:CXX>:${KERNEL_CXX_FLAGS}>)
target_compile_options(kernel PUBLIC $<$<COMPILE_LANGUAGE:ASM>:${KERNEL_ASM_FLAGS}>)

target_include_directories(kernel PRIVATE src ${LIMINE_DIR} ${FLANTERM_DIR}/src ${CHDRS_INC_DIR} ${CXXHDRS_INC_DIR} ${NANOPRINTF_DIR} ${HOR_UACPI_INCLUDES})

set_target_properties(kernel PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})

target_link_libraries(kernel PRIVATE kernel-common)