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

    enum BuddySafeFreeStatus {
        BUDDY_SAFE_FREE_SUCCESS,
        BUDDY_SAFE_FREE_BUDDY_IS_NULL,
        BUDDY_SAFE_FREE_INVALID_ADDRESS,
        BUDDY_SAFE_FREE_SIZE_MISMATCH,
        BUDDY_SAFE_FREE_ALREADY_FREE,
    };

    enum BuddyTreeReleaseStatus {
        BUDDY_TREE_RELEASE_SUCCESS,
        BUDDY_TREE_RELEASE_FAIL_PARTIALLY_USED,
    };

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

    struct BuddyChangeTracker {
        void *ctx;
        void (*tracker)(void *, u8 *, u64);
    };

    struct BuddyTreePos {
        u64 index {};
        u64 depth {};
    };

    struct BuddyTreeInterval {
        BuddyTreePos from {};
        BuddyTreePos to {};
    };

    struct BuddyTreeWalkState {
        BuddyTreePos startingPos {};
        BuddyTreePos currentPos {};
        u32 goingUp {};
        u32 walkDone {};
    };

    struct BuddyTree {};

    #define INVALID_POS BuddyTreePos{ 0, 0 }

    #define BUDDY_ALLOC_ALIGN (sizeof(u64) * CHAR_BIT)

    #define BUDDY_ALIGNOF(x) alignof(x)

    class BuddyAllocator {
    public:
        BuddyAllocator() = default;
        ~BuddyAllocator() = default;

        /*
         * Initialization functions
         */

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

        /* Initializes a buddy allocation tree at the specified location */
        static BuddyTree *treeInit(u8 *at, u8 order);

        /* Indicates whether this is a valid position for the tree */
        static bool treeValid(BuddyTree *tree, BuddyTreePos pos);

        /* Returns the order of the specified buddy allocation tree */
        static u8 treeOrder(BuddyTree *tree);

        /*
         * Resize the tree to the new order. When downsizing the left subtree is picked.
         * Caller must ensure enough space for the new order.
         */
        static void treeResize(BuddyTree *tree, u8 desiredPos);

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

        /* Returns the size of a buddy allocation tree of the desired order*/
        static u64 buddyTreeSizeOf(u8 order);

        /*
         * Allocation functions
         */

        /* Use the specified buddy to allocate memory. See malloc. */
        static void *malloc(BuddyContext *ctx, u64 requestedSize);

        /* Use the specified buddy to allocate zeroed memory. See calloc. */
        static void *calloc(BuddyContext *ctx, u64 membersCount, u64 membersSize);

        /* Realloc semantics are a joke. See realloc. */
        static void *reAlloc(BuddyContext *ctx, void *ptr, u64 requestedSize, bool ignoreData);

        /* Realloc-like behavior that checks for overflow. See reAllocArray */
        static void *reAllocArray(BuddyContext *ctx, void *ptr, u64 membersCount, u64 membersSize, bool ignoreData);

        /* Use the specified buddy to free memory. See free. */
        static void free(BuddyContext *ctx, void *ptr);

        /* Returns the free capacity at or underneath the indicated position */
        static u64 treeStatus(BuddyTree *tree, BuddyTreePos pos);

        /* Marks the indicated position as allocated and propagates the change */
        static void treeMark(BuddyTree *tree, BuddyTreePos pos);

        /* Marks the indicated position as free and propagates the change */
        static BuddyTreeReleaseStatus treeRelease(BuddyTree *tree, BuddyTreePos pos);

        /* Returns a free position at the specified depth or an invalid position */
        static BuddyTreePos treeFindFree(BuddyTree *tree, u8 depth);

        /* Tests if the indicated position is available for allocation */
        static bool treeIsFree(BuddyTree *tree, BuddyTreePos pos);

        /* Tests if the tree can be shrunk in half */
        static bool treeCanShrink(BuddyTree *tree);

        /*
         * Reservation functions
         */

        /* A (safer) free with a size. Will not free unless the size fits the target span. */
        static BuddySafeFreeStatus buddySafeFree(BuddyContext *ctx, void *ptr, u64 requestedSize);

        /* Reserve a range by marking it as allocated. Useful for dealing with physical memory. */
        static void reserveRange(BuddyContext *ctx, void *ptr, u64 requestedSize);

        /* Release a reserved memory range. Unsafe, this can mess up other allocations if called with wrong parameters! */
        static void unsafeReleaseRange(BuddyContext *ctx, void *ptr, u64 requestedSize);

        /*
         * Iteration functions
         */

        /*
         * Iterate through the free and allocated slots and call the provided function for each of them.
         *
         * If the provided function returns a non-NULL result the iteration stops and the result
         * is returned to called. NULL is returned upon completing iteration without stopping.
         *
         * The iteration order is implementation-defined and may change between versions.
         */
        static void *walk(BuddyContext *ctx, void *(fp)(void *ctx, void *addr, u64 slotSize, u64 allocated), void *otherCtx);

        /*
         * Miscellaneous functions
         */

        /*
         * Calculates the fragmentation in the allocator in a 0 - 255 range.
         * NOTE: if you are using a non-power-of-two sized arena the maximum upper bound can be lowed.
         */
        static u8 fragmentation(BuddyContext *ctx);

        /*
         * Enable change tracking for this allocator instance.
         *
         * This will store a header at the start of the arena that contains the function pointer (tracker) and
         * a void* (context). The tracker will be called with the context, the start of changed memory and its length.
         *
         * This function MUST be called before any allocations are performed!
         *
         * Change tracking is in effect only for allocation functions, resizing functions are excluded from it.
         *
         * This is an experimental feature designed to facilitate integration with https://github.com/spaskalev/libpvl
         *
         * The API is not (yet) part of the allocator contract and its semantic versioning!
         */
        static void enableChangeTracking(BuddyContext *ctx, void *otherCtx, void (*tracker)(void *, u8 *, u64));

        /* Enable change tracking state for this tree. */
        static void treeEnableChangeTracking(BuddyTree *tree, u8 desiredOrder);

        /* Get a pointer to the parent buddy struct */
        static BuddyContext *treeBuddy(BuddyTree *tree);

        /*
         * Debug functions
         */

        /* Implementation defined */
        static void debug(BuddyContext *ctx);

        /* Implementation defined */
        static void treeDebug(BuddyTree *tree, BuddyTreePos pos, u64 startSize);

        /* Implementation defined */
        static u32 treeCheckInvariant(BuddyTree *tree, BuddyTreePos pos);

        /* Report fragmentation in a 0 - 255 range */
        static u8 treeFragmentation(BuddyTree *tree);

        /*
         * Navigation functions
         */

        /* Returns a position at the root of a buddy allocation tree */
        static BuddyTreePos treeRoot();

        /* Returns the leftmost child node */
        static BuddyTreePos treeLeftmostChild(BuddyTree *tree);

        /* Returns the tree depth of the indicated position */
        static u64 treeDepth(BuddyTreePos pos);

        /* Returns the left child node position. Does not check if that is a valid position */
        static BuddyTreePos treeLeftChild(BuddyTreePos pos);

        /* Returns the right child node position. Does not check if that is a valid position */
        static BuddyTreePos treeRightChild(BuddyTreePos pos);

        /* Returns the current sibling node position. Does not check if that is a valid position */
        static BuddyTreePos treeSibling(BuddyTreePos pos);

        /* Returns the parent node position or an invalid position if there is no parent node */
        static BuddyTreePos treeParent(BuddyTreePos pos);

        /* Returns the right adjacent node position or an invalid position if there is no right adjacent node */
        static BuddyTreePos treeRightAdjacent(BuddyTreePos pos);

        /* Returns the at-depth index of the indicated position */
        static u64 treeIndex(BuddyTreePos pos);

        /* Return the interval of the deepest positions spanning the indicated position */
        static BuddyTreeInterval treeInterval(BuddyTree *tree, BuddyTreePos pos);

        /* Checks if one interval contains another */
        static bool treeIntervalContains(BuddyTreeInterval outer, BuddyTreeInterval inner);

        /* Return a walk state structure starting from the root of a tree */
        static BuddyTreeWalkState treeWalkStateRoot();

        /* Walk the tree, keeping track in the provided state structure */
        static u32 treeWalk(BuddyTree *tree, BuddyTreeWalkState *state);
    };
}

#endif