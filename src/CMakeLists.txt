cmake_minimum_required(VERSION ${HORIZON_CMAKE_VERSION})

set(HOR_LIBS_INC libs/hos-lib/src libs/hos-base/src)

add_subdirectory(kernel)
add_subdirectory(libs)