cmake_minimum_required(VERSION ${HORIZON_CMAKE_VERSION})

# ------------------ LLVM Compiler ------------------
set(LLVM_REPO "https://github.com/llvm/llvm-project.git" CACHE STRING "LLVM Repository")
set(LLVM_BRANCH "release/20.x" CACHE STRING "LLVM Branch")
set(LLVM_DIR "${ROOT_DIR}/deps/llvm" CACHE PATH "LLVM Directory")
set(LLVM_BUILD_DIR "${LLVM_DIR}/build" CACHE PATH "LLVM Build Directory")
set(LLVM_BIN_DIR "${LLVM_BUILD_DIR}/bin" CACHE PATH "LLVM Bin Directory")

function(CloneLLVM)
    if(NOT EXISTS "${LLVM_DIR}" AND ${HORIZON_ARCH} STREQUAL "x86_64" AND ${CMAKE_BUILD_TYPE} STREQUAL "Debug")
        message(STATUS "Cloning LLVM repository...")
        execute_process(
                COMMAND git clone --depth=1 --branch ${LLVM_BRANCH} ${LLVM_REPO} ${LLVM_DIR}
                WORKING_DIRECTORY ${ROOT_DIR}
                RESULT_VARIABLE GIT_CLONE_RESULT
        )

        if(NOT GIT_CLONE_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to clone LLVM repository.")
        endif()
    endif()
endfunction()

function(BuildLLVM)
    if(NOT EXISTS "${LLVM_BUILD_DIR}" AND ${HORIZON_ARCH} STREQUAL "x86_64" AND ${CMAKE_BUILD_TYPE} STREQUAL "Debug")
        file(MAKE_DIRECTORY ${LLVM_BUILD_DIR})
        message(STATUS "Configuring LLVM build...")
        execute_process(
                COMMAND "cmake -S ${LLVM_DIR}/llvm -B ${LLVM_BUILD_DIR} -G Ninja -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_PROJECTS=\"clang;clang-tools-extra;lld;lldb\" -DLLVM_TARGETS_TO_BUILD=\"X86;RISCV;AArch64\" -DLLVM_ENABLE_ASSERTIONS=OFF -DCMAKE_INSTALL_PREFIX=${LLVM_DIR}/bin -DLLVM_PARALLEL_COMPILE_JOBS=20 -DLLVM_PARALLEL_LINK_JOBS=20"
                WORKING_DIRECTORY ${ROOT_DIR}
                RESULT_VARIABLE CMAKE_CONFIGURE_RESULT
        )

        if(NOT CMAKE_CONFIGURE_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to configure LLVM build.")
        endif()

        message(STATUS "Building LLVM...")
        execute_process(
                COMMAND "cmake --build ${LLVM_BUILD_DIR} --parallel"
                WORKING_DIRECTORY ${ROOT_DIR}
                RESULT_VARIABLE CMAKE_BUILD_RESULT
        )
        if(NOT CMAKE_BUILD_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to build LLVM.")
        endif()
    endif()
endfunction()

CloneLLVM()
BuildLLVM()

#include(${LLVM_BUILD_DIR}/cmake/modules/CMakeFiles/LLVMConfig.cmake)

#message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
#message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

#include_directories(${LLVM_INCLUDE_DIRS})
#separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
#add_definitions(${LLVM_DEFINITIONS_LIST})

#llvm_map_components_to_libnames(LLVM_LIBS support core irreader)

# ------------------ Tools Setup ------------------
set(CMAKE_LINKER "${LLVM_BIN_DIR}/ld.lld" CACHE FILEPATH "Linker")
set(COMMON_LINKER "<CMAKE_LINKER> <LINK_FLAGS> -melf_${HORIZON_ARCH} <OBJECTS> -o <TARGET> <LINK_LIBRARIES>" CACHE STRING "Common Linker")
set(CMAKE_C_LINK_EXECUTABLE "${COMMON_LINKER}" CACHE STRING "C Linker")
set(CMAKE_CXX_LINK_EXECUTABLE "${COMMON_LINKER}" CACHE STRING "C++ Linker")
set(CMAKE_ASM_LINK_EXECUTABLE "${COMMON_LINKER}" CACHE STRING "Asm Linker")

set(CMAKE_C_COMPILER "${LLVM_BIN_DIR}/clang" CACHE FILEPATH "C Compiler")
set(CMAKE_C_COMPILER_AR "${LLVM_BIN_DIR}/llvm-ar" CACHE FILEPATH "C Compiler AR")
set(CMAKE_CXX_COMPILER "${LLVM_BIN_DIR}/clang++" CACHE FILEPATH "C++ Compiler")
set(CMAKE_CXX_COMPILER_AR  "${LLVM_BIN_DIR}/clang" CACHE FILEPATH "C++ Compiler AR")
set(CMAKE_ASM_COMPILER "${LLVM_BIN_DIR}/clang" CACHE FILEPATH "Asm Compiler")
set(CMAKE_ASM_COMPILER_AR "${LLVM_BIN_DIR}/llvm-ar" CACHE FILEPATH "Asm Compiler AR")
set(CMAKE_AR "${LLVM_BIN_DIR}/llvm-ar" CACHE FILEPATH "AR Tool")

# ------------------ C Headers ------------------
set(CHDRS_REPO "https://codeberg.org/osdev/freestnd-c-hdrs.git" CACHE STRING "CHDRS Repository")
set(CHDRS_BRANCH "trunk" CACHE STRING "CHDRS Branch")
set(CHDRS_DIR "${ROOT_DIR}/deps/chdrs" CACHE PATH "CHDRS Directory")
set(CHDRS_INC_DIR "${CHDRS_DIR}/${HORIZON_ARCH}/include" CACHE PATH "CHDRS Include Directory")

function(CloneChdrs)
    if(NOT EXISTS "${CHDRS_DIR}" AND ${HORIZON_ARCH} STREQUAL "x86_64" AND ${CMAKE_BUILD_TYPE} STREQUAL "Debug")
        message(STATUS "Cloning C Headers repository...")
        execute_process(
                COMMAND git clone --depth=1 --branch ${CHDRS_BRANCH} ${CHDRS_REPO} ${CHDRS_DIR}
                WORKING_DIRECTORY ${ROOT_DIR}
                RESULT_VARIABLE GIT_CLONE_RESULT
        )

        if(NOT GIT_CLONE_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to clone CHDRS repository.")
        endif()
    endif()
endfunction()

CloneChdrs()

include_directories(BEFORE ${CHDRS_INC_DIR})

# ------------------ uACPI ------------------
set(UACPI_REPO "https://github.com/uACPI/uACPI.git" CACHE STRING "uACPI Repository")
set(UACPI_DIR "${ROOT_DIR}/deps/uacpi" CACHE PATH "uACPI Directory")

function(CloneUAcpi)
    if(NOT EXISTS "${UACPI_DIR}" AND ${HORIZON_ARCH} STREQUAL "x86_64" AND ${CMAKE_BUILD_TYPE} STREQUAL "Debug")
        message(STATUS "Cloning uACPI repository...")
        execute_process(
                COMMAND git clone --depth=1 ${UACPI_REPO} ${UACPI_DIR}
                WORKING_DIRECTORY ${ROOT_DIR}
                RESULT_VARIABLE GIT_CLONE_RESULT
        )

        if(NOT GIT_CLONE_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to clone uACPI repository.")
        endif()
    endif()
endfunction()

CloneUAcpi()

include(${UACPI_DIR}/uacpi.cmake)

set(HOR_UACPI_INCLUDES ${UACPI_INCLUDES} CACHE STRING "uACPI Includes")
set(HOR_UACPI_SOURCES ${UACPI_SOURCES} CACHE STRING "uACPI Sources")

# ------------------ Limine Bootloader ------------------
set(LIMINE_REPO "https://github.com/limine-bootloader/limine.git" CACHE STRING "Limine Repository")
set(LIMINE_BRANCH "v9.x-binary" CACHE STRING "Limine Branch")
set(LIMINE_DIR "${ROOT_DIR}/deps/limine" CACHE PATH "Limine Directory")

function(CloneLimine)
    if(NOT EXISTS "${LIMINE_DIR}" AND ${HORIZON_ARCH} STREQUAL "x86_64" AND ${CMAKE_BUILD_TYPE} STREQUAL "Debug")
        message(STATUS "Cloning Limine repository...")
        execute_process(
                COMMAND git clone --depth=1 --branch ${LIMINE_BRANCH} ${LIMINE_REPO} ${LIMINE_DIR}
                WORKING_DIRECTORY ${ROOT_DIR}
                RESULT_VARIABLE GIT_CLONE_RESULT
        )

        if(NOT GIT_CLONE_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to clone Limine repository.")
        endif()
    endif()
endfunction()

function(BuildLimine)
    if(NOT EXISTS "${LIMINE_DIR}/limine" AND ${HORIZON_ARCH} STREQUAL "x86_64" AND ${CMAKE_BUILD_TYPE} STREQUAL "Debug")
        message(STATUS "Building Limine...")
        execute_process(
                COMMAND make
                WORKING_DIRECTORY ${LIMINE_DIR}
                RESULT_VARIABLE LIMINE_BUILD_RESULT
        )

        if(NOT LIMINE_BUILD_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to build Limine.")
        endif()
    endif()
endfunction()

CloneLimine()
BuildLimine()

# ------------------ Flanterm ------------------
set(FLANTERM_REPO "https://codeberg.org/mintsuki/flanterm.git" CACHE STRING "Flanterm Repository")
set(FLANTERM_BRANCH "trunk" CACHE STRING "Flanterm Branch")
set(FLANTERM_DIR "${ROOT_DIR}/deps/flanterm" CACHE PATH "Flanterm Directory")

function(CloneFlanterm)
    if(NOT EXISTS "${FLANTERM_DIR}" AND ${HORIZON_ARCH} STREQUAL "x86_64" AND ${CMAKE_BUILD_TYPE} STREQUAL "Debug")
        message(STATUS "Cloning Flanterm repository...")
        execute_process(
                COMMAND git clone --depth=1 --branch ${FLANTERM_BRANCH} ${FLANTERM_REPO} ${FLANTERM_DIR}
                WORKING_DIRECTORY ${ROOT_DIR}
                RESULT_VARIABLE GIT_CLONE_RESULT
        )

        if(NOT GIT_CLONE_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to clone Flanterm repository.")
        endif()
    endif()
endfunction()

CloneFlanterm()

include_directories(${FLANTERM_DIR})

# ------------------ OVMF Bios ------------------
set(OVMF_FILE "${ROOT_DIR}/deps/ovmf/${HORIZON_ARCH}/OVMF.fd" CACHE FILEPATH "OVMF File")

function(DownloadOVMF)
    if (${HORIZON_ARCH} STREQUAL "x86_64")
        set(OVMF_LINK "https://retrage.github.io/edk2-nightly/bin/RELEASEX64_OVMF.fd" CACHE STRING "OVMF Link")
    elseif (${HORIZON_ARCH} STREQUAL "riscv64")
        set(OVMF_LINK "https://retrage.github.io/edk2-nightly/bin/RELEASERISCV64_VIRT_CODE.fd" CACHE STRING "OVMF Link")
    elseif (${HORIZON_ARCH} STREQUAL "aarch64")
        set(OVMF_LINK "https://retrage.github.io/edk2-nightly/bin/RELEASEAARCH64_QEMU_EFI.fd" CACHE STRING "OVMF Link")
    endif ()

    if (NOT EXISTS "${OVMF_FILE}")
        file(DOWNLOAD "${OVMF_LINK}" "${OVMF_FILE}")
    endif ()

    if (${HORIZON_ARCH} STREQUAL "riscv64" OR ${HORIZON_ARCH} STREQUAL "aarch64")
        execute_process(
                COMMAND dd if=/dev/zero of=${OVMF_FILE} bs=1 count=0 seek=67108864
        )
    endif ()
endfunction()

DownloadOVMF()

# ------------------ NanoPrintf ------------------
set(NANOPRINTF_LINK "https://raw.githubusercontent.com/charlesnicholson/nanoprintf/refs/heads/main/nanoprintf.h" CACHE STRING "NanoPrintf Link")
set(NANOPRINTF_DIR "${ROOT_DIR}/deps/nanoprintf" CACHE PATH "NanoPrintf Directory")
set(NANOPRINTF_FILE "${NANOPRINTF_DIR}/nanoprintf.h" CACHE PATH "NanoPrintf File")

function(DownloadNanoPrintf)
    if(NOT EXISTS "${NANOPRINTF_DIR}" AND ${HORIZON_ARCH} STREQUAL "x86_64" AND ${CMAKE_BUILD_TYPE} STREQUAL "Debug")
        message(STATUS "Downloading NanoPrintf...")
        execute_process(
                COMMAND mkdir ${NANOPRINTF_DIR}
                WORKING_DIRECTORY ${ROOT_DIR}
        )

        file(DOWNLOAD "${NANOPRINTF_LINK}" "${NANOPRINTF_FILE}")
    endif()
endfunction()

DownloadNanoPrintf()

include_directories(${NANOPRINTF_DIR})

# ------------------ Update ------------------
add_custom_target(Update-Main-Deps
        COMMAND rm -rf ${TMP_ISO_DIR}
        COMMAND cd ${CHDRS_DIR} && git pull
        COMMAND cd ${LIMINE_DIR} && git pull
        COMMAND cd ${FLANTERM_DIR} && git pull
        WORKING_DIRECTORY ${ROOT_DIR}
)

add_custom_target(Update-Compiler
        COMMAND cd ${LLVM_DIR} && git pull
        WORKING_DIRECTORY ${ROOT_DIR}
)