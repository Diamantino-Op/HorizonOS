#ifndef KERNEL_COMMON_VIRTUALALLOCATOR_HPP
#define KERNEL_COMMON_VIRTUALALLOCATOR_HPP

#include "VirtualMemory.hpp"

#include "SpinLock.hpp"

namespace kernel::common::memory {
    constexpr u8 minBlockSize = 64;

    struct __attribute__((packed)) MemoryBlock {
        usize size {};
        bool free {};
        MemoryBlock *next {};
    };

    struct AllocContext {
        PageMap pageMap {};
        u8 pageFlags {};
        u64 *heapStart {};
		usize heapSize {};
		MemoryBlock *blocks {};
        TicketSpinLock lock {};
        bool isUserspace {};
    };

    class VirtualAllocator {
    public:
		static AllocContext *createContext(bool isUserspace);
        static void destroyContext(AllocContext *ctx);

        static void initContext(const AllocContext *ctx);

        static u64 getPhysicalAddress(u64 virtualAddress);

        static u64 *alloc(AllocContext *ctx, u64 size);
        static void free(AllocContext *ctx, u64 *ptr);

        static void defrag(const AllocContext *ctx);

    private:
        static void growHeap(AllocContext *ctx, u64 minSize);
        static void shrinkHeap(AllocContext *ctx);
    };
}

#endif