# ------------------ Global Variables ------------------
set(HORIZON_ARCH "x86_64" CACHE STRING "HorizonOS Architecture")
set(HORIZON_VERSION 0.0.1 CACHE STRING "HorizonOS Version")
set(HORIZON_CMAKE_VERSION 3.28 CACHE STRING "HorizonOS CMake Version")
set(HORIZON_CXX_STANDARD 23 CACHE STRING "HorizonOS C++ Standard")
set(HORIZON_C_STANDARD 23 CACHE STRING "HorizonOS C Standard")

set(ROOT_DIR "${CMAKE_SOURCE_DIR}" CACHE PATH "Root Directory")

cmake_minimum_required(VERSION ${HORIZON_CMAKE_VERSION})

set(LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/res/${HORIZON_ARCH}/Linker.ld" CACHE FILEPATH "Linker Script")

# ------------------ Dependencies Setup ------------------
add_subdirectory(deps)

# ------------------ ISO Directory ------------------
set(ISO_DIR "${CMAKE_SOURCE_DIR}/iso/${HORIZON_ARCH}")
set(RES_DIR "${CMAKE_SOURCE_DIR}/res")

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

# ------------------ Source ------------------
add_subdirectory(src)

# ------------------ ISO ------------------
set(ISO_OUTPUT "${CMAKE_SOURCE_DIR}/iso/out/HorizonOS-${HORIZON_ARCH}.iso")

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
    set(X86_64_RUN_FLAGS "-cpu max,+hypervisor,+invtsc,+tsc-deadline -smp 10 -M q35 -m 8G -accel kvm -bios ${OVMF_FILE} -cdrom ${ISO_OUTPUT}")

    add_custom_target(
            Run-HorizonOS-Debug
            COMMAND qemu-system-x86_64 -s -S ${X86_64_RUN_FLAGS}
            DEPENDS HorizonOS-ISO
    )

    add_custom_target(
            Run-HorizonOS-NoDbg
            COMMAND qemu-system-x86_64 ${X86_64_RUN_FLAGS}
            DEPENDS HorizonOS-ISO
    )
elseif (${HORIZON_ARCH} STREQUAL "riscv64")
    set(RISCV_RUN_FLAGS "-cpu rv64 -smp 10 -M virt -m 8G -usb -device qemu-xhci -device usb-kbd -device usb-mouse -device ramfb -device ahci,id=ahci -device ide-hd,drive=hdd0 -drive if=pflash,unit=0,format=raw,file=${OVMF_FILE} -drive file=${ISO_OUTPUT},if=none,id=hdd0")

    add_custom_target(
            Run-HorizonOS-Debug
            COMMAND qemu-system-riscv64 -s -S ${RISCV_RUN_FLAGS}
            DEPENDS HorizonOS-ISO
    )

    add_custom_target(
            Run-HorizonOS-NoDbg
            COMMAND qemu-system-riscv64 ${RISCV_RUN_FLAGS}
            DEPENDS HorizonOS-ISO
    )
elseif (${HORIZON_ARCH} STREQUAL "aarch64")
    set (AARCH64_RUN_FLAGS "-cpu cortex-a76 -smp 10 -M virt -m 8G -usb -device qemu-xhci -device usb-kbd -device usb-mouse -device ramfb -device ahci,id=ahci -drive if=pflash,unit=0,format=raw,file=${OVMF_FILE} -cdrom ${ISO_OUTPUT}")

    add_custom_target(
            Run-HorizonOS-Debug
            COMMAND qemu-system-aarch64 -s -S ${AARCH64_RUN_FLAGS}
            DEPENDS HorizonOS-ISO
    )

    add_custom_target(
            Run-HorizonOS-NoDbg
            COMMAND qemu-system-aarch64 ${AARCH64_RUN_FLAGS}
            DEPENDS HorizonOS-ISO
    )
endif ()