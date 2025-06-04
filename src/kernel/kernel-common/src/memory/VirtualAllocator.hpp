#ifndef KERNEL_COMMON_VIRTUALALLOCATOR_HPP
#define KERNEL_COMMON_VIRTUALALLOCATOR_HPP

#include "VirtualMemory.hpp"

#include "SpinLock.hpp"

namespace kernel::common::memory {
    constexpr u8 minBlockSize = 64;

    struct __attribute__((aligned(64))) MemoryBlock {
        usize size {};
        bool free {};
        MemoryBlock *next {};
    };

    struct AllocContext {
        PageMap pageMap {};
        u8 pageFlags {};
        u64 *heapStart {};
		usize heapSize {};
        u64 freeSpace {};
		MemoryBlock *blocks {};
        SimpleSpinLock lock {};
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

        static void initContext(AllocContext *ctx);

        static u64 getPhysicalAddress(u64 virtualAddress);

        static u64 *alloc(AllocContext *ctx, u64 size);
        static void free(AllocContext *ctx, u64 *ptr);

        static void defrag(AllocContext *ctx);

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

    // New Allocator

    struct BuddyContext {
        u64 memorySize {};
        u64 alignment {};

        union {
            u8 *main;
            ptrDiff mainOffset;
        } arena;

        u64 buddyFlags {};
    };

    struct BuddyEmbedCheck {
        u32 canFit {};
        u64 offset {};
        u64 buddySize {};
    };

    class BuddyAllocator {
    public:
        BuddyAllocator() = default;
        ~BuddyAllocator() = default;

        /* Initializes a binary buddy memory allocator at the specified location */
        static BuddyContext *init(u8 *at, u8 *main, u64 memSize);

        /* Initializes a binary buddy memory allocator at the specified location using a non-default alignment */
        static BuddyContext *initAlignment(u8 *at, u8 *main, u64 memSize, u64 alignment);

        /*
         * Initializes a binary buddy memory allocator embedded in the specified arena.
         * The arena's capacity is reduced to account for the allocator metadata.
         */
        static BuddyContext *initEmbed(u8 *main, u64 memSize);

        /*
         * Returns the address of a previously created buddy allocator at the arena.
         * Use to get a new handle to the allocator when the arena is moved or copied.
         */
        static BuddyContext *getEmbedAt(u8 *main, u64 memSize);

        /*
         * Returns the address of a previously created buddy allocator at the arena.
         * Use to get a new handle to the allocator when the arena is moved or copied.
         */
        static BuddyContext *getEmbedAtAlignment(u8 *main, u64 memSize, u64 alignment);

        /*
         * Initializes a binary buddy memory allocator embedded in the specified arena
         * using a non-default alignment.
         * The arena's capacity is reduced to account for the allocator metadata.
         */
        static BuddyContext *initEmbedAlignment(u8 *main, u64 memSize, u64 alignment);

        /*
         * Resizes the arena and allocator metadata to a new size.
         *
         * Existing allocations are preserved. If an allocation is to fall outside
         * the arena after a downsizing, the resize operation fails.
         *
         * Returns a pointer to allocator on successful resize. This will be
         * the same pointer when the allocator is external to the arena. If the
         * allocator is embedded in the arena, the old pointer to the allocator
         * must not be used after resizing!
         *
         * Returns NULL on failure. The allocations and allocator pointer
         * are preserved.
         */
        static BuddyContext *resize(BuddyContext *ctx, u64 newSize);

        /* Tests if the arena can be shrunk in half */
        static bool canShrink(BuddyContext *ctx);

        /* Tests if the arena is completely empty */
        static bool isEmpty(BuddyContext *ctx);

        /* Tests if the arena is completely full */
        static bool isFull(BuddyContext *ctx);

        static u64 getArenaSize(BuddyContext *ctx);

        /*
         * Reports the arena's free size. Note that this is (often) not a continuous size
         * but the sum of all free slots in the buddy.
         */
        static u64 getArenaFreeSize(BuddyContext *ctx);

        /* Returns the size of a buddy required to manage a block of the specified size */
        static u64 buddySizeOf(u64 memSize);

        /*
         * Returns the size of a buddy required to manage a block of the specified size
         * using a non-default alignment.
         */
        static u64 buddySizeOfAlignment(u64 memSize, u64 alignment);

        /*
         * Allocation functions
         */

        /* Use the specified buddy to allocate memory. See malloc. */
        void *buddyMalloc(BuddyContext *ctx, u64 requestedSize);

        /* Use the specified buddy to allocate zeroed memory. See calloc. */
        void *buddyCalloc(BuddyContext *ctx, u64 membersCount, u64 membersSize);

        /* Realloc semantics are a joke. See realloc. */
        void *buddyReAlloc(BuddyContext *ctx, void *ptr, u64 requestedSize, bool ignoreData);

        /* Realloc-like behavior that checks for overflow. See reAllocArray */
        void *buddyReAllocArray(BuddyContext *ctx, void *ptr, u64 membersCount, u64 membersSize, bool ignoreData);

        /* Use the specified buddy to free memory. See free. */
        void *buddyFree(BuddyContext *ctx, void *ptr);
    };
}

#endif