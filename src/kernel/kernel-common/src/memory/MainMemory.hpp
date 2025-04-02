#ifndef KERNEL_COMMON_MAINMEMORY_HPP
#define KERNEL_COMMON_MAINMEMORY_HPP

#include "Types.hpp"

namespace kernel::common::memory {
    extern "C" void *memcpy(void *dest, const void *src, usize n);

    extern "C" void *memset(void *s, int c, usize n);

    extern "C" void *memmove(void *dest, const void *src, usize n);

    extern "C" int memcmp(const void *s1, const void *s2, usize n);
}

#endif