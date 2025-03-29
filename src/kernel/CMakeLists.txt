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

set_target_properties(kernel PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})

target_include_directories(kernel-common PUBLIC ${LIMINE_DIR} ${FLANTERM_DIR})
target_include_directories(kernel PUBLIC ${LIMINE_DIR} ${FLANTERM_DIR})

target_link_libraries(kernel-common hos-base)
target_link_libraries(kernel kernel-common)