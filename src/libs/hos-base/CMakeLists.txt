cmake_minimum_required(VERSION 3.28)
set(CMAKE_CXX_STANDARD 23)

project(hos-base VERSION 0.0.1 LANGUAGES CXX)

# ------------------ Kernel Common ------------------
file(GLOB_RECURSE HOS_BASE_SRC "src/*.hpp" "src/*.cpp" "src/*.asm")

add_library(hos-base STATIC ${HOS_BASE_SRC})

target_include_directories(hos-base PUBLIC src)