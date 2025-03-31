cmake_minimum_required(VERSION ${HORIZON_CMAKE_VERSION})
set(CMAKE_CXX_STANDARD ${HORIZON_CXX_STANDARD})
set(CMAKE_C_STANDARD ${HORIZON_C_STANDARD})

project(kernel-common VERSION ${HORIZON_VERSION} LANGUAGES CXX C ASM)

file(GLOB_RECURSE KERNEL_COMMON_SRC "src/*.hpp" "src/*.cpp" "src/*.asm" "src/*.s")
file(GLOB_RECURSE FLANTERM_SRC "${FLANTERM_DIR}/*.h" "${FLANTERM_DIR}/*.c")

add_library(kernel-common STATIC ${KERNEL_COMMON_SRC} ${FLANTERM_SRC})

target_link_options(kernel-common PUBLIC ${KERNEL_LINK_FLAGS})
target_compile_options(kernel-common PUBLIC $<$<COMPILE_LANGUAGE:C>:${KERNEL_C_FLAGS}>)
target_compile_options(kernel-common PUBLIC $<$<COMPILE_LANGUAGE:CXX>:${KERNEL_CXX_FLAGS}>)
target_compile_options(kernel-common PUBLIC $<$<COMPILE_LANGUAGE:ASM>:${KERNEL_ASM_FLAGS}>)

target_include_directories(kernel-common PUBLIC src ${LIMINE_DIR} ${FLANTERM_DIR})

target_link_libraries(kernel-common hos-base)