cmake_minimum_required(VERSION ${HORIZON_CMAKE_VERSION})
set(CMAKE_CXX_STANDARD ${HORIZON_CXX_STANDARD})
set(CMAKE_C_STANDARD ${HORIZON_C_STANDARD})

project(uacpi-service VERSION ${HORIZON_VERSION} LANGUAGES C CXX ASM)

# ------------------ Kernel Common ------------------
file(GLOB_RECURSE UACPI_SERVICE_SRC "src/*.hpp" "src/*.cpp" "src/*.asm")

add_executable(uacpi-service ${HOS_BASE_SRC})

target_include_directories(uacpi-service PUBLIC src ${CHDRS_INC_DIR} ${CXXHDRS_INC_DIR})