cmake_minimum_required(VERSION ${HORIZON_CMAKE_VERSION})
set(CMAKE_CXX_STANDARD ${HORIZON_CXX_STANDARD})
set(CMAKE_C_STANDARD ${HORIZON_C_STANDARD})

project(hos-lib VERSION ${HORIZON_VERSION} LANGUAGES C CXX ASM)

# ------------------ Kernel Common ------------------
file(GLOB_RECURSE HOS_LIB_SRC "src/*.hpp" "src/*.cpp" "src/*.asm")

add_library(hos-lib STATIC ${HOS_LIB_SRC})

target_include_directories(hos-lib PUBLIC src ${CHDRS_INC_DIR} ${CXXHDRS_INC_DIR})