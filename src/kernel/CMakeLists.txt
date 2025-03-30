cmake_minimum_required(VERSION ${HORIZON_CMAKE_VERSION})

set(KERNEL_C_FLAGS "" CACHE STRING "Kernel C flags")
set(KERNEL_CXX_FLAGS "" CACHE STRING "Kernel C++ flags")
set(KERNEL_ASM_FLAGS "" CACHE STRING "Kernel Asm flags")
set(KERNEL_LINK_FLAGS "" CACHE STRING "Kernel Link flags")

add_subdirectory(kernel-common)
add_subdirectory("kernel-${HORIZON_ARCH}")