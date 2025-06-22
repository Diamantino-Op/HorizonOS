#ifndef KERNEL_COMMON_VIRTUALALLOCATOR_HPP
#define KERNEL_COMMON_VIRTUALALLOCATOR_HPP

#include "VirtualMemory.hpp"

#include "SpinLock.hpp"

namespace kernel::common::memory {
#ifndef HORIZON_USE_NEW_ALLOCATOR
    constexpr u8 minBlockSize = 64;

    struct __attribute__((aligned(64))) MemoryBlock {
        usize size {};
        bool free {};
        MemoryBlock *next {};
    };
#endif

#ifdef HORIZON_USE_NEW_ALLOCATOR
    class LibAlloc;
#endif

    struct AllocContext {
        PageMap pageMap {};
        u8 pageFlags {};
        u64 *heapStart {};
        usize heapSize {};

#ifndef HORIZON_USE_NEW_ALLOCATOR
        u64 freeSpace {};
        MemoryBlock *blocks {};
        SimpleSpinLock lock {};
#else
        LibAlloc *libAlloc {};
#endif

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

#ifndef HORIZON_USE_NEW_ALLOCATOR
        static void initContext(AllocContext *ctx);

        static u64 getPhysicalAddress(u64 virtualAddress);
#endif

        static u64 *alloc(AllocContext *ctx, u64 size);
        static void free(AllocContext *ctx, u64 *ptr);

#ifndef HORIZON_USE_NEW_ALLOCATOR
        static void defrag(AllocContext *ctx);

    private:
        static void growHeap(AllocContext *ctx, u64 minSize);
        static void shrinkHeap(AllocContext *ctx);
#endif
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

#ifdef HORIZON_USE_NEW_ALLOCATOR
    #define ALIGNMENT 64

    #define ALIGN_TYPE char

    #define ALIGN_INFO sizeof(ALIGN_TYPE) * 16

    #define LIBALLOC_MAGIC	0xc001c0de
    #define LIBALLOC_DEAD	0xdeaddead

    struct LibAllocMinor;

    /**
     * A structure found at the top of all system-allocated
     * memory blocks. It details the usage of the memory block.
     */
    struct /*__attribute__((aligned(ALIGNMENT)))*/ LibAllocMajor {
        LibAllocMajor *prev; ///< Linked list information.
        LibAllocMajor *next; ///< Linked list information.
        u32 pages; ///< The number of pages in the block.
        u32 size; ///< The number of pages in the block.
        u32 usage; ///< The number of bytes used in the block.
        LibAllocMinor *first; ///< A pointer to the first allocated memory in the block.
    };

    /**
     * This is a structure found at the beginning of all
     * sections in a major block which were allocated by a
     * malloc, calloc, realloc call.
     */
    struct /*__attribute__((aligned(ALIGNMENT)))*/ LibAllocMinor {
        LibAllocMinor *prev; ///< Linked list information.
        LibAllocMinor *next; ///< Linked list information.
        LibAllocMajor *block; ///< The owning block. A pointer to the major structure.
        u32 magic; ///< A magic number to identify correctness.
        u32 size; ///< The size of the memory allocated. Could be 1 byte or more.
        u32 reqSize; ///< The size of memory requested.
    };

    class LibAlloc {
    public:
		LibAlloc() = default;
		explicit LibAlloc(AllocContext *ctx);
        ~LibAlloc() = default;

        void setCtx(AllocContext *ctx);

        /**
         * This is the hook into the local system which allocates pages. It
         * accepts an integer parameter which is the number of pages
         * required.  The page size was set up in the init function.
         *
         * @return nullptr if the pages were not allocated.
         * @return A pointer to the allocated memory.
         */
        u64 *alloc(usize reqSize) const;

        /**
         * This frees previously allocated memory. The u64* parameter passed
         * to the function is the exact same value returned from a previous
         * alloc call.
         *
         * The integer value is the number of pages to free.
         *
         * @return 0 if the memory was successfully freed.
         */
        u32 free(u64 *ptr, usize reqSize) const;

        u64 *malloc(usize reqSize);

        u64 *realloc(u64 *ptr, usize reqSize);

        u64 *calloc(usize count, usize reqSize);

        void free(u64 *ptr);

        LibAllocMajor *allocateNewPage(u32 size);

        void dump() const;

		static u64 *align(u64 *ptr);

        static u64 *unalign(u64 *ptr);

    private:
        LibAllocMajor *memRoot {}; ///< The root memory block acquired from the system.
        LibAllocMajor *bestBet {}; ///< The major with the most free memory.

        const u32 pageCount = 16;

        u64 allocated {}; ///< Running total of allocated memory.
        u64 inUse {}; ///< Running total of used memory.

        u64 warningCount {}; ///< Number of warnings encountered
        u64 errorCount {}; ///< Number of actual errors
        u64 possibleOverruns {}; ///< Number of possible overruns

        SimpleSpinLock lock {};

        AllocContext *ctx {};
    };
#endif
}

#endif