#ifndef KERNEL_COMMON_PHYSICALMEMORY_HPP
#define KERNEL_COMMON_PHYSICALMEMORY_HPP

#include "SpinLock.hpp"
#include "Types.hpp"

namespace kernel::common::memory {
    constexpr u16 pageSize = 0x1000;

	struct AllocContext;

    struct PmmListEntry {
        PmmListEntry *prev;
        usize count;
        PmmListEntry *next;
    };

    class PhysicalMemoryManager {
    public:
        PhysicalMemoryManager() = default;

        void init();

    	void reclaimMemory();

        u64 *allocPages(usize pageAmount, bool useHhdm);

        void freePages(u64 *virtAddress, usize pageAmount);

    	void freePagesCtx(const AllocContext *ctx, u64 *virtAddress, usize pageAmount);

    	void freePagesPhys(const u64 *physAddress, usize pageAmount);

        u64 getFreeMemory() const;

    	u64 getTotalMemory() const;

    private:
        bool addFreeRangePhys(u64 phys, usize pageAmount, bool clearPages, bool addToTotal, const char *source);
        void removeFreeEntry(PmmListEntry *entry);

        PmmListEntry *listPtr {};
        TicketSpinLock pmmSpinLock {};

        u64 totalMemory {};
        bool bootloaderMemoryReclaimed {};
    };
}

#endif
