#ifndef KERNEL_COMMON_PHYSICALMEMORY_HPP
#define KERNEL_COMMON_PHYSICALMEMORY_HPP

#include "Types.hpp"

namespace kernel::common::memory {
    constexpr u16 pageSize = 0x1000;
    constexpr u64 currHhdm = 0xffff800000000000;

    struct PmmListEntry {
        u64 address {}; // 64th bit = used, 63rd bit = empty
    };

    class PhysicalMemoryManager {
    public:
        PhysicalMemoryManager() = default;

        void init();

        u64 *allocPages(usize pageAmount, bool useHhdm);

        void freePages(u64 *virtAddress);

    private:
        uPtr *listPtr {};
        u64 listSize {};
    };
}

#endif