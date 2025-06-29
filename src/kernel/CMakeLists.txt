cmake_minimum_required(VERSION ${HORIZON_CMAKE_VERSION})

set(EXTRA_C_FLAGS "")
set(EXTRA_CXX_FLAGS "")
set(EXTRA_ASM_FLAGS "")

if (${HORIZON_ARCH} STREQUAL "x86_64")
    if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
        set(EXTRA_C_FLAGS -O0 -g -ggdb3 -fsanitize=undefined -DHORIZON_DEBUG) # -O0 -DHORIZON_USE_NEW_ALLOCATOR
    else ()
        set(EXTRA_C_FLAGS -O3 -ffast-math)
    endif ()

    set(EXTRA_C_FLAGS ${EXTRA_C_FLAGS} -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -march=x86-64 -m64 -mno-80387 -fPIC -fPIE)
    #set(EXTRA_CXX_FLAGS)
    set(EXTRA_ASM_FLAGS -mno-sse -mno-sse2 -mno-mmx -march=x86-64 -m64 -mno-80387)
elseif (${HORIZON_ARCH} STREQUAL "riscv64")
    #set(EXTRA_C_FLAGS)
    #set(EXTRA_CXX_FLAGS)
    #set(EXTRA_ASM_FLAGS)
elseif (${HORIZON_ARCH} STREQUAL "aarch64")
    #set(EXTRA_C_FLAGS)
    #set(EXTRA_CXX_FLAGS)
    #set(EXTRA_ASM_FLAGS)
endif ()

set(KERNEL_C_FLAGS -DLIMINE_API_REVISION=3 -Wall -Wextra -Werror -nostdinc -ffreestanding -fno-exceptions -fno-rtti -fno-omit-frame-pointer -Wno-error=constant-logical-operand -Wno-error=tautological-undefined-compare -Wno-error=unused-variable -Wno-error=unused-function -Wno-error=unused-parameter -Wno-error=unused-private-field -Wno-error=cast-function-type-mismatch -Wno-error=deprecated-enum-enum-conversion -static --target=${HORIZON_ARCH}-none-elf ${EXTRA_C_FLAGS} CACHE STRING "Kernel C Flags")
set(KERNEL_CXX_FLAGS ${KERNEL_C_FLAGS} ${EXTRA_CXX_FLAGS} CACHE STRING "Kernel C++ Flags")
set(KERNEL_ASM_FLAGS -static --target=${HORIZON_ARCH}-none-elf -masm=intel ${EXTRA_ASM_FLAGS} CACHE STRING "Kernel Asm Flags")
set(KERNEL_LINK_FLAGS -T ${LINKER_SCRIPT} -static -nostdlib CACHE STRING "Kernel Link Flags")

add_subdirectory(kernel-common)
add_subdirectory("kernel-${HORIZON_ARCH}")