cmake_minimum_required(VERSION 3.28)

include(FetchContent)

project(HorizonOS)

# ------------------ LLVM Compiler ------------------
set(LLVM_REPO "https://github.com/llvm/llvm-project.git")
set(LLVM_BRANCH "release/20.x")
set(LLVM_DIR "${CMAKE_SOURCE_DIR}/llvm")
set(LLVM_BUILD_DIR "${LLVM_DIR}/build")
set(LLVM_BIN_DIR "${LLVM_BUILD_DIR}/bin")

# Clone LLVM if it does not exist
if(NOT EXISTS "${LLVM_DIR}")
    message(STATUS "Cloning LLVM repository...")
    execute_process(
            COMMAND git clone --depth=1 --branch ${LLVM_BRANCH} ${LLVM_REPO} ${LLVM_DIR}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            RESULT_VARIABLE GIT_CLONE_RESULT
    )
    if(NOT GIT_CLONE_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to clone LLVM repository.")
    endif()
endif()

# Build LLVM if not built
if(NOT EXISTS "${LLVM_BUILD_DIR}")
    file(MAKE_DIRECTORY ${LLVM_BUILD_DIR})
    message(STATUS "Configuring LLVM build...")
    execute_process(
            COMMAND cmake -S ${LLVM_DIR}/llvm -B ${LLVM_BUILD_DIR} -G Ninja -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_PROJECTS=clang;clang-tools-extra;lld;lldb; -DLLVM_ENABLE_RUNTIMES=libc;libcxx;libcxxabi; -DLLVM_ENABLE_ASSERTIONS=OFF -DCMAKE_INSTALL_PREFIX=${LLVM_DIR}/bin
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            RESULT_VARIABLE CMAKE_CONFIGURE_RESULT
    )
    if(NOT CMAKE_CONFIGURE_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to configure LLVM build.")
    endif()

    message(STATUS "Building LLVM...")
    execute_process(
            COMMAND cmake --build ${LLVM_BUILD_DIR} --parallel
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            RESULT_VARIABLE CMAKE_BUILD_RESULT
    )
    if(NOT CMAKE_BUILD_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to build LLVM.")
    endif()
endif()

# ------------------ Tools Setup ------------------
set(CMAKE_C_COMPILER ${LLVM_BIN_DIR}/clang CACHE FILEPATH "C Compiler")
set(CMAKE_CXX_COMPILER ${LLVM_BIN_DIR}/clang++ CACHE FILEPATH "C++ Compiler")
set(CMAKE_ASM_COMPILER ${LLVM_BIN_DIR}/clang CACHE FILEPATH "Asm Compiler")
set(CMAKE_AR ${LLVM_BIN_DIR}/llvm-ar CACHE FILEPATH "AR Tool")

# ------------------ Global Variables ------------------
set(HORIZON_ARCH "x86_64" CACHE STRING "HorizonOS Architecture")
set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/res/${HORIZON_ARCH}/Linker.ld CACHE FILEPATH "Linker Script")
set(CMAKE_C_FLAGS -Wall -Wextra -O2 -fPIE -fshort-wchar --target=${HORIZON_ARCH}-none-elf CACHE STRING "C Flags")
set(CMAKE_CXX_FLAGS ${CMAKE_C_FLAGS} CACHE STRING "C++ Flags")
set(CMAKE_ASM_FLAGS ${CMAKE_C_FLAGS} CACHE STRING "Asm Flags")
set(CMAKE_EXE_LINKER_FLAGS ${CMAKE_EXE_LINKER_FLAGS} -T ${LINKER_SCRIPT} -pie -nostdlib CACHE STRING "Linker Flags")

# ------------------ Kernel ------------------
add_subdirectory(src/kernel)

# ------------------ ISO ------------------
# Compile C, C++, and Assembly files
set(ISO_OUTPUT "${CMAKE_BINARY_DIR}/HorizonOS.iso")

# Generate ISO
add_custom_command(
        OUTPUT ${ISO_OUTPUT}
        COMMAND mkdir -p ${CMAKE_BINARY_DIR}/iso/boot
        COMMAND cp ${CMAKE_BINARY_DIR}/HorizonOS ${CMAKE_BINARY_DIR}/iso/boot/HorizonOS
        COMMAND xorriso -as mkisofs -R -J -V "HorizonOS" -b boot/HorizonOS -no-emul-boot -boot-load-size 4 -boot-info-table -o ${ISO_OUTPUT} ${CMAKE_BINARY_DIR}/iso
        DEPENDS HorizonOS
        COMMENT "Generating bootable ISO"
)

add_custom_target(iso ALL DEPENDS ${ISO_OUTPUT})
