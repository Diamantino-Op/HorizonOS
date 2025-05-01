#ifndef KERNEL_COMMON_PHYSICALMEMORY_HPP
#define KERNEL_COMMON_PHYSICALMEMORY_HPP

#include "Types.hpp"
#include "SpinLock.hpp"

namespace kernel::common::memory {
    constexpr u16 pageSize = 0x1000;

    struct PmmListEntry {
        PmmListEntry *prev;
        usize count;
        PmmListEntry *next;
    };

    class PhysicalMemoryManager {
    public:
        PhysicalMemoryManager() = default;

        void init();

        u64 *allocPages(usize pageAmount, bool useHhdm);

        void freePages(u64 *virtAddress, usize pageAmount);

        u64 getFreeMemory() const;

    private:
        PmmListEntry *listPtr {};
        TicketSpinLock pmmSpinLock {};
    };
}

#endif