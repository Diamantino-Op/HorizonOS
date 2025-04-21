#ifndef KERNEL_COMMON_MAINMEMORY_HPP
#define KERNEL_COMMON_MAINMEMORY_HPP

#include "Types.hpp"

namespace kernel::common::memory {
    extern "C" void *memcpy(void *destAddr, const void *srcAddr, usize size);
    extern "C" void *memset(void *addr, int val, usize size);
    extern "C" void *memmove(void *destAddr, const void *srcAddr, usize size);
    extern "C" int memcmp(const void *addr1, const void *addr2, usize size);

    extern "C" void* malloc(usize size);
    extern "C" void free(void* ptr);
}

void* operator new(usize size);
void* operator new[](usize size);
void operator delete(void* ptr) noexcept;
void operator delete(void* ptr, usize) noexcept;
void operator delete[](void* ptr) noexcept;
void operator delete[](void* ptr, usize) noexcept;

#endif