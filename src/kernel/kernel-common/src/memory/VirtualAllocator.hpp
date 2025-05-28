#ifndef KERNEL_COMMON_VIRTUALALLOCATOR_HPP
#define KERNEL_COMMON_VIRTUALALLOCATOR_HPP

#include "VirtualMemory.hpp"

#include "SpinLock.hpp"

namespace kernel::common::memory {
    struct __attribute__((packed, aligned(64))) MemoryBlock {
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

    struct VpaListEntry {
        VpaListEntry *prev {};
        u64 base {};
        u64 count {};
        bool isAllocated {};
        VpaListEntry *next {};
    };

    class VirtualAllocator {
    public:
		static AllocContext *createContext(bool isUserspace, bool isProcess);
        static void destroyContext(AllocContext *ctx);

        static void shareKernelPages(const AllocContext *ctx);

        static void initContext(const AllocContext *ctx);

        static u64 getPhysicalAddress(u64 virtualAddress);

        static u64 *alloc(AllocContext *ctx, u64 size);
        static void free(AllocContext *ctx, u64 *ptr);

        static void defrag(const AllocContext *ctx);

    private:
        static void growHeap(AllocContext *ctx, u64 minSize);
        static void shrinkHeap(AllocContext *ctx);
    };

    class VirtualPageAllocator {
    public:
        VirtualPageAllocator() = default;
        ~VirtualPageAllocator() = default;

        void init(u64 kernAddr);

        u64 *allocVPages(u64 amount) const;

        void freeVPages(const u64 *addr) const;

    private:
        VpaListEntry *vPagesListPtr {};
    };
}

#endif