cmake_minimum_required(VERSION 3.28)

add_subdirectory(kernel-common)

if (${HORIZON_ARCH} STREQUAL "x86_64")
    add_subdirectory(kernel-x86_64)
elseif (${HORIZON_ARCH} STREQUAL "riscv64")
    add_subdirectory(kernel-riscv64)
elseif (${HORIZON_ARCH} STREQUAL "aarch64")
    add_subdirectory(kernel-aarch64)
else ()
    message("Invalid Arch")
endif ()

set_target_properties(kernel PROPERTIES EXECUTABLE_OUTPUT_PATH "${ISO_DIR}/boot/HorizonOS")
set_target_properties(kernel PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})

target_link_libraries(kernel-common ${LLVM_LIBS} hos-base)
target_link_libraries(kernel ${LLVM_LIBS} kernel-common)