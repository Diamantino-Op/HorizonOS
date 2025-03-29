cmake_minimum_required(VERSION 3.28)

include(FetchContent)

# ------------------ Global Variables ------------------
set(HORIZON_ARCH "x86_64" CACHE STRING "HorizonOS Architecture")

# ------------------ LLVM Compiler ------------------
set(LLVM_REPO "https://github.com/llvm/llvm-project.git" CACHE STRING "LLVM Repository")
set(LLVM_BRANCH "release/20.x" CACHE STRING "LLVM Branch")
set(LLVM_DIR "${CMAKE_SOURCE_DIR}/deps/llvm" CACHE PATH "LLVM Directory")
set(LLVM_BUILD_DIR "${LLVM_DIR}/build" CACHE PATH "LLVM Build Directory")
set(LLVM_BIN_DIR "${LLVM_BUILD_DIR}/bin" CACHE PATH "LLVM Bin Directory")

# Clone LLVM
if(NOT EXISTS "${LLVM_DIR}")
    message(STATUS "Cloning LLVM repository...")
    execute_process(
            COMMAND git clone --depth=1 --branch ${LLVM_BRANCH} ${LLVM_REPO} ${LLVM_DIR}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            RESULT_VARIABLE GIT_CLONE_RESULT
    )
    execute_process(
            COMMAND rm -rf ${LLVM_DIR}/.git
            WORKING_DIRECTORY ${LLVM_DIR}
    )
    if(NOT GIT_CLONE_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to clone LLVM repository.")
    endif()
endif()

# Build LLVM
if(NOT EXISTS "${LLVM_BUILD_DIR}")
    file(MAKE_DIRECTORY ${LLVM_BUILD_DIR})
    message(STATUS "Configuring LLVM build...")
    execute_process(
            COMMAND "cmake -S ${LLVM_DIR}/llvm -B ${LLVM_BUILD_DIR} -G Ninja -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_PROJECTS=\"clang;clang-tools-extra;lld;lldb\" -DLLVM_TARGETS_TO_BUILD=\"X86;RISCV;AArch64\" -DLLVM_ENABLE_ASSERTIONS=OFF -DCMAKE_INSTALL_PREFIX=${LLVM_DIR}/bin -DLLVM_PARALLEL_COMPILE_JOBS=12 -DLLVM_PARALLEL_LINK_JOBS=12"
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            RESULT_VARIABLE CMAKE_CONFIGURE_RESULT
    )
    if(NOT CMAKE_CONFIGURE_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to configure LLVM build.")
    endif()

    message(STATUS "Building LLVM...")
    execute_process(
            COMMAND "cmake --build ${LLVM_BUILD_DIR} --parallel"
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            RESULT_VARIABLE CMAKE_BUILD_RESULT
    )
    if(NOT CMAKE_BUILD_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to build LLVM.")
    endif()
endif()

include(${LLVM_BUILD_DIR}/cmake/modules/CMakeFiles/LLVMConfig.cmake)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

#include_directories(${LLVM_INCLUDE_DIRS})
#separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
#add_definitions(${LLVM_DEFINITIONS_LIST})

#llvm_map_components_to_libnames(LLVM_LIBS support core irreader)

# ------------------ Tools Setup ------------------
set(LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/res/${HORIZON_ARCH}/Linker.ld" CACHE FILEPATH "Linker Script")

set(COMMON_C_FLAGS "-Wall -Wextra -O3 -ffreestanding -static --target=${HORIZON_ARCH}-none-elf" CACHE STRING "Common C Flags")
set(COMMON_ASM_FLAGS "-static --target=${HORIZON_ARCH}-none-elf -masm=intel" CACHE STRING "Common Asm Flags")

if (${HORIZON_ARCH} STREQUAL "x86_64")
    message("X86_64 Flags")

    set(CMAKE_C_FLAGS "${COMMON_C_FLAGS} -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -march=x86-64 -m64 -mno-80387" CACHE STRING "C Flags")
    set(CMAKE_ASM_FLAGS "${COMMON_ASM_FLAGS} -mno-sse -mno-sse2 -mno-mmx -march=x86-64 -m64 -mno-80387" CACHE STRING "Asm Flags")
elseif (${HORIZON_ARCH} STREQUAL "riscv64")
    message("RiscV64 Flags")

    set(CMAKE_C_FLAGS "${COMMON_C_FLAGS}" CACHE STRING "C Flags")
    set(CMAKE_ASM_FLAGS "${COMMON_ASM_FLAGS}7" CACHE STRING "Asm Flags")
elseif (${HORIZON_ARCH} STREQUAL "aarch64")
    message("AArch64 Flags")

    set(CMAKE_C_FLAGS "${COMMON_C_FLAGS}" CACHE STRING "C Flags")
    set(CMAKE_ASM_FLAGS "${COMMON_ASM_FLAGS}" CACHE STRING "Asm Flags")
endif ()

set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS}" CACHE STRING "C++ Flags")

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -T ${LINKER_SCRIPT} -static -nostdlib" CACHE STRING "Linker Flags")

message("C Flags: ${CMAKE_C_FLAGS}")
message("C++ Flags: ${CMAKE_CXX_FLAGS}")
message("Asm Flags: ${CMAKE_ASM_FLAGS} \n")

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
set(CHDRS_REPO "https://github.com/osdev0/freestnd-c-hdrs.git" CACHE STRING "CHDRS Repository")
set(CHDRS_BRANCH "trunk" CACHE STRING "CHDRS Branch")
set(CHDRS_DIR "${CMAKE_SOURCE_DIR}/deps/chdrs" CACHE PATH "CHDRS Directory")
set(CHDRS_INC_DIR "${CHDRS_DIR}/${HORIZON_ARCH}/include" CACHE PATH "CHDRS Include Directory")

if(NOT EXISTS "${CHDRS_DIR}")
    message(STATUS "Cloning C Headers repository...")
    execute_process(
            COMMAND git clone --depth=1 --branch ${CHDRS_BRANCH} ${CHDRS_REPO} ${CHDRS_DIR}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            RESULT_VARIABLE GIT_CLONE_RESULT
    )
    execute_process(
            COMMAND rm -rf ${CHDRS_DIR}/.git
            WORKING_DIRECTORY ${CHDRS_DIR}
    )
    if(NOT GIT_CLONE_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to clone CHDRS repository.")
    endif()
endif()

include_directories(${CHDRS_INC_DIR})

# ------------------ uACPI ------------------
set(UACPI_REPO "https://github.com/uACPI/uACPI.git" CACHE STRING "uACPI Repository")
set(UACPI_DIR "${CMAKE_SOURCE_DIR}/deps/uacpi" CACHE PATH "uACPI Directory")

if(NOT EXISTS "${UACPI_DIR}")
    message(STATUS "Cloning uACPI repository...")
    execute_process(
            COMMAND git clone --depth=1 ${UACPI_REPO} ${UACPI_DIR}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            RESULT_VARIABLE GIT_CLONE_RESULT
    )
    execute_process(
            COMMAND rm -rf ${UACPI_DIR}/.git
            WORKING_DIRECTORY ${UACPI_DIR}
    )
    if(NOT GIT_CLONE_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to clone uACPI repository.")
    endif()
endif()

include(${UACPI_DIR}/uacpi.cmake)

# ------------------ Limine Bootloader ------------------
set(LIMINE_REPO "https://github.com/limine-bootloader/limine.git" CACHE STRING "Limine Repository")
set(LIMINE_BRANCH "v9.x-binary" CACHE STRING "Limine Branch")
set(LIMINE_DIR "${CMAKE_SOURCE_DIR}/deps/limine" CACHE PATH "Limine Directory")

# Clone Limine
if(NOT EXISTS "${LIMINE_DIR}")
    message(STATUS "Cloning Limine repository...")
    execute_process(
            COMMAND git clone --depth=1 --branch ${LIMINE_BRANCH} ${LIMINE_REPO} ${LIMINE_DIR}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            RESULT_VARIABLE GIT_CLONE_RESULT
    )
    execute_process(
            COMMAND rm -rf ${LIMINE_DIR}/.git
            WORKING_DIRECTORY ${LIMINE_DIR}
    )
    if(NOT GIT_CLONE_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to clone Limine repository.")
    endif()
endif()

# Build Limine
if(NOT EXISTS "${LIMINE_DIR}/limine")
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

# ------------------ OVMF Bios ------------------
set(OVMF_FILE "${CMAKE_SOURCE_DIR}/deps/ovmf/${HORIZON_ARCH}/OVMF.fd" CACHE FILEPATH "OVMF File")

if (${HORIZON_ARCH} STREQUAL "x86_64")
    set(OVMF_LINK "https://retrage.github.io/edk2-nightly/bin/RELEASEX64_OVMF.fd" CACHE STRING "OVMF Link")
elseif (${HORIZON_ARCH} STREQUAL "riscv64")
    set(OVMF_LINK "https://retrage.github.io/edk2-nightly/bin/RELEASERISCV64_VIRT_CODE.fd" CACHE STRING "OVMF Link")

    execute_process(
            COMMAND dd if=/dev/zero of=${OVMF_FILE} bs=1 count=0 seek=33554432
    )
elseif (${HORIZON_ARCH} STREQUAL "aarch64")
    set(OVMF_LINK "https://retrage.github.io/edk2-nightly/bin/RELEASEAARCH64_QEMU_EFI.fd" CACHE STRING "OVMF Link")

    execute_process(
            COMMAND dd if=/dev/zero of=${OVMF_FILE} bs=1 count=0 seek=33554432
    )
endif ()

if (NOT EXISTS "${OVMF_FILE}")
    file(DOWNLOAD "${OVMF_LINK}" "${OVMF_FILE}")
endif ()

# ------------------ ISO Directory ------------------
set(ISO_DIR "${CMAKE_SOURCE_DIR}/iso/${HORIZON_ARCH}" CACHE PATH "ISO Directory")
set(RES_DIR "${CMAKE_SOURCE_DIR}/res" CACHE PATH "Resources Directory")

if (NOT EXISTS "${ISO_DIR}")
    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/iso/out" "${ISO_DIR}" "${ISO_DIR}/efi/boot" "${ISO_DIR}/boot" "${ISO_DIR}/boot/limine" "${ISO_DIR}/boot/HorizonOS")

    if (${HORIZON_ARCH} STREQUAL "x86_64")
        file(COPY "${LIMINE_DIR}/BOOTX64.EFI" DESTINATION "${ISO_DIR}/efi/boot")
    elseif (${HORIZON_ARCH} STREQUAL "riscv64")
        file(COPY "${LIMINE_DIR}/BOOTRISCV64.EFI" DESTINATION "${ISO_DIR}/efi/boot")
    elseif (${HORIZON_ARCH} STREQUAL "aarch64")
        file(COPY "${LIMINE_DIR}/BOOTAA64.EFI" DESTINATION "${ISO_DIR}/efi/boot")
    endif ()

    file(COPY "${RES_DIR}/Limine.conf" DESTINATION "${ISO_DIR}/boot/limine")
    file(COPY "${LIMINE_DIR}/limine-bios.sys" DESTINATION "${ISO_DIR}/boot/limine")
    file(COPY "${LIMINE_DIR}/limine-bios-cd.bin" DESTINATION "${ISO_DIR}/boot/limine")
    file(COPY "${LIMINE_DIR}/limine-bios-pxe.bin" DESTINATION "${ISO_DIR}/boot/limine")
    file(COPY "${LIMINE_DIR}/limine-uefi-cd.bin" DESTINATION "${ISO_DIR}/boot/limine")
endif ()

# ------------------ Project Setup ------------------
project(HorizonOS)

# ------------------ Kernel ------------------
add_subdirectory(src/kernel)

# ------------------ Libs ------------------
add_subdirectory(src/libs/hos-base)

# ------------------ ISO ------------------
set(ISO_OUTPUT "${CMAKE_SOURCE_DIR}/iso/out/HorizonOS-${HORIZON_ARCH}.iso" CACHE FILEPATH "ISO Output")

# Generate ISO
add_custom_command(
        OUTPUT ${ISO_OUTPUT}
        COMMAND cp $<TARGET_FILE:kernel> ${ISO_DIR}/boot/HorizonOS/HorizonOS_Kernel
        COMMAND xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus -apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin -efi-boot-part --efi-boot-image --protective-msdos-label ${ISO_DIR} -o ${ISO_OUTPUT}
        COMMAND ${LIMINE_DIR}/limine bios-install ${ISO_OUTPUT}
        DEPENDS kernel
        COMMENT "Generating bootable ISO"
)

add_custom_target(HorizonOS-ISO ALL DEPENDS ${ISO_OUTPUT})

# ------------------ Qemu Target ------------------

# Enter gdb: gdb iso/x86_64/boot/HorizonOS/HorizonOS_Kernel
# GDB Cmd: gdb target extended-remote localhost:1234

if (${HORIZON_ARCH} STREQUAL "x86_64")
    add_custom_target(
            Run-HorizonOS-Debug
            COMMAND qemu-system-x86_64 -s -S -cpu max,+hypervisor,+invtsc,+tsc-deadline -smp 10 -M q35 -m 8G -accel kvm -bios ${OVMF_FILE} -cdrom ${ISO_OUTPUT} -serial stdio
            DEPENDS HorizonOS-ISO
    )

    add_custom_target(
            Run-HorizonOS-NoDbg
            COMMAND qemu-system-x86_64 -cpu max,+hypervisor,+invtsc,+tsc-deadline -smp 10 -M q35 -m 8G -accel kvm -bios ${OVMF_FILE} -cdrom ${ISO_OUTPUT} -serial stdio
            DEPENDS HorizonOS-ISO
    )
elseif (${HORIZON_ARCH} STREQUAL "riscv64")
    add_custom_target(
            Run-HorizonOS-Debug
            COMMAND qemu-system-riscv64 -s -S -cpu rv64 -smp 10 -M virt -m 8G -usb -device qemu-xhci -device usb-kbd -device usb-mouse -device ramfb -device ahci,id=ahci -device ide-hd,drive=hdd0 -drive if=pflash,unit=0,format=raw,file=${OVMF_FILE} -drive file=${ISO_OUTPUT},if=none,id=hdd0 -serial stdio
            DEPENDS HorizonOS-ISO
    )

    add_custom_target(
            Run-HorizonOS-NoDbg
            COMMAND qemu-system-riscv64 -cpu rv64 -smp 10 -M virt -m 8G -usb -device qemu-xhci -device usb-kbd -device usb-mouse -device ramfb -device ahci,id=ahci -device ide-hd,drive=hdd0 -drive if=pflash,unit=0,format=raw,file=${OVMF_FILE} -drive file=${ISO_OUTPUT},if=none,id=hdd0 -serial stdio
            DEPENDS HorizonOS-ISO
    )
elseif (${HORIZON_ARCH} STREQUAL "aarch64")
    add_custom_target(
            Run-HorizonOS-Debug
            COMMAND qemu-system-aarch64 -s -S -cpu cortex-a76 -smp 10 -M virt -m 8G -usb -device qemu-xhci -device usb-kbd -device usb-mouse -device ramfb -device ahci,id=ahci -device ide-hd,drive=hdd0 -drive if=pflash,unit=0,format=raw,file=${OVMF_FILE} -drive file=${ISO_OUTPUT},if=none,id=hdd0 -serial stdio
            DEPENDS HorizonOS-ISO
    )

    add_custom_target(
            Run-HorizonOS-NoDbg
            COMMAND qemu-system-aarch64 -cpu cortex-a76 -smp 10 -M virt -m 8G -usb -device qemu-xhci -device usb-kbd -device usb-mouse -device ramfb -device ahci,id=ahci -device ide-hd,drive=hdd0 -drive if=pflash,unit=0,format=raw,file=${OVMF_FILE} -drive file=${ISO_OUTPUT},if=none,id=hdd0 -serial stdio
            DEPENDS HorizonOS-ISO
    )
endif ()