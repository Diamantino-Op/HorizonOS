#ifndef KERNEL_COMMON_PHYSICALMEMORY_HPP
#define KERNEL_COMMON_PHYSICALMEMORY_HPP

#include "Types.hpp"

namespace kernel::common::memory {
    constexpr u16 pageSize = 0x1000;

    struct UsableMemory {
        u64 start {};
        u64 size {};
    };

    class PhysicalMemoryManager {
    public:
        PhysicalMemoryManager() = default;

        void init();
    };
}

#endif