#ifndef KERNEL_COMMON_VIRTUALALLOCATOR_HPP
#define KERNEL_COMMON_VIRTUALALLOCATOR_HPP

#include "SpinLock.hpp"
#include "VirtualMemory.hpp"
#include "cstdint"

namespace kernel::common::memory {
    constexpr u8 minBlockSize = 64;

	constexpr u16 tableEntryCount = 512;
	constexpr u16 rootUserEntryCount = 256;
	constexpr u64 pageShift = 12;
	constexpr usize hugePage1GCount = 512ULL * 512ULL;
	constexpr usize hugePage2MCount = 512ULL;
	constexpr usize kernelHeapReservedSize = 10ULL * 1024ULL * 1024ULL * 1024ULL;

	constexpr usize SIZE_CLASS_COUNT = 9;
	constexpr usize sizeClasses[SIZE_CLASS_COUNT] = {64, 128, 256, 512, 1024, 2048, 4096, 16384, SIZE_MAX};

    struct __attribute__((aligned(64))) MemoryBlock {
    	MemoryBlock *freePrev {};
    	MemoryBlock *prev {};
        usize size {};
        bool free {};
        MemoryBlock *next {};
    	MemoryBlock *freeNext {};
    };

    struct __attribute__((aligned(64))) AllocContext {
        PageMap pageMap;
        u8 pageFlags {};
        u64 *heapStart {};
        usize heapSize {};
        u64 freeSpace {};
        MemoryBlock *blocks {};
        MemoryBlock *lastBlock {};
        MemoryBlock *freeLists[SIZE_CLASS_COUNT] {};
        SimpleSpinLock lock {};
        u64 heapReserveEnd {};
        u64 heapSecondReserveStart {};
        u64 heapSecondReserveEnd {};
        u64 heapReservedEnd {};
        u64 heapCursor {};
        bool usesReservedHeap {};

        bool isUserspace {};
    };

    struct VpaListEntry {
        VpaListEntry *prev {};
        u64 base {};
        u64 count {};
        bool isAllocated {};
        VpaListEntry *next {};
    };

    struct CreatedContext {
        AllocContext *ctx {};
        AllocContext *ctkKern {};
    };

    class VirtualAllocator {
    public:
        static AllocContext *createContext();
        static CreatedContext createProcessContext();

        static void destroyContext(AllocContext *ctx);

        static void shareKernelPages(const AllocContext *ctx);

        static void initContext(AllocContext *ctx);

        static u64 getPhysicalAddress(u64 virtualAddress);

        static u64 *alloc(AllocContext *ctx, u64 size, bool isUserAlloc = false);
        static void free(AllocContext *ctx, u64 *ptr);

        static void defrag(AllocContext *ctx, MemoryBlock *block);

    	static u64 getProcessAllocStart();

    private:
        static u64 startCreateArch();
        static void endCreateArch(u64 value);
        static void freePageTableChildren(const u64 *tableAddr, bool level5Paging, u8 depth, u64 skipPhysA, u64 skipPhysB);

        static bool growHeap(AllocContext *ctx, u64 minSize, bool isUserAlloc);
        static void shrinkHeap(AllocContext *ctx);

    	static usize getSizeClassIndex(usize size);

    	static void insertFreeList(AllocContext *ctx, MemoryBlock *block);
    	static void removeFreeList(AllocContext *ctx, MemoryBlock *block);

    	static bool areAdjacent(const MemoryBlock *left, const MemoryBlock *right);

    public:
    	static bool isPagingLvl5;
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
