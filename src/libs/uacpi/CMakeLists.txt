cmake_minimum_required(VERSION ${HORIZON_CMAKE_VERSION})
set(CMAKE_CXX_STANDARD ${HORIZON_CXX_STANDARD})
set(CMAKE_C_STANDARD ${HORIZON_C_STANDARD})

project(uacpi VERSION ${HORIZON_VERSION} LANGUAGES CXX C ASM)

add_library(uacpi STATIC ${HOR_UACPI_SOURCES})

set(UACPI_EXTRA_C_FLAGS "")
set(UACPI_EXTRA_CXX_FLAGS "")

if (${HORIZON_ARCH} STREQUAL "x86_64")
    if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
        set(UACPI_EXTRA_C_FLAGS -g)
    endif ()

    set(UACPI_EXTRA_C_FLAGS ${UACPI_EXTRA_C_FLAGS} -march=x86-64 -m64 -fPIC -fPIE)
    #set(UACPI_EXTRA_CXX_FLAGS)
elseif (${HORIZON_ARCH} STREQUAL "riscv64")
    #set(UACPI_EXTRA_C_FLAGS)
    #set(UACPI_EXTRA_CXX_FLAGS)
elseif (${HORIZON_ARCH} STREQUAL "aarch64")
    #set(UACPI_EXTRA_C_FLAGS)
    #set(UACPI_EXTRA_CXX_FLAGS)
endif ()

set(UACPI_LINK_FLAGS -T ${LINKER_SCRIPT} -static -nostdlib CACHE STRING "uAcpi Link Flags")
set(UACPI_C_FLAGS ${UACPI_EXTRA_C_FLAGS} -ffreestanding -O3 -ffast-math -nostdinc -static --target=${HORIZON_ARCH}-none-elf CACHE STRING "uAcpi C Flags")
set(UACPI_CXX_FLAGS ${UACPI_EXTRA_CXX_FLAGS} ${UACPI_C_FLAGS} CACHE STRING "uAcpi C++ Flags")

target_link_options(uacpi PUBLIC ${UACPI_LINK_FLAGS})
target_compile_options(uacpi PUBLIC $<$<COMPILE_LANGUAGE:C>:${UACPI_C_FLAGS}>)
target_compile_options(uacpi PUBLIC $<$<COMPILE_LANGUAGE:CXX>:${UACPI_CXX_FLAGS}>)

target_include_directories(uacpi PUBLIC ${HOR_UACPI_INCLUDES} ${CHDRS_INC_DIR})