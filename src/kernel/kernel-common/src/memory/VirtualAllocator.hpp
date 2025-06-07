#ifndef KERNEL_COMMON_VIRTUALALLOCATOR_HPP
#define KERNEL_COMMON_VIRTUALALLOCATOR_HPP

#include "VirtualMemory.hpp"

#include "SpinLock.hpp"

#include "limits.h"

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

    constexpr u32 buddyRelativeMode = 1;

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

    enum BuddyTreeFlags {
        BUDDY_TREE_CHANGE_TRACKING = 1,
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

    struct InternalPosition {
        u64 localOffset {};
        u64 bitSetOffset {};
    };

    #define INVALID_POS BuddyTreePos{ 0, 0 }

    #define BUDDY_ALLOC_ALIGN (sizeof(u64) * CHAR_BIT)

    #define BUDDY_ALIGNOF(x) alignof(x)

    class BuddyTree;

    class Buddy {
    public:
        /*
         * Initialization functions
         */

        /* Initializes a binary buddy memory allocator at the specified location */
        static Buddy *init(u8 *at, u8 *main, u64 memSize);

        /* Initializes a binary buddy memory allocator at the specified location using a non-default alignment */
        static Buddy *initAlignment(u8 *at, u8 *main, u64 memSize, u64 alignment);

        /*
         * Initializes a binary buddy memory allocator embedded in the specified arena.
         * The arena's capacity is reduced to account for the allocator metadata.
         */
        static Buddy *initEmbed(u8 *main, u64 memSize);

        /*
         * Initializes a binary buddy memory allocator embedded in the specified arena
         * using a non-default alignment.
         * The arena's capacity is reduced to account for the allocator metadata.
         */
        static Buddy *initEmbedAlignment(u8 *main, u64 memSize, u64 alignment);

        /*
         * Returns the address of a previously created buddy allocator at the arena.
         * Use to get a new handle to the allocator when the arena is moved or copied.
         */
        static Buddy *getEmbedAt(u8 *main, u64 memSize);

        /*
         * Returns the address of a previously created buddy allocator at the arena.
         * Use to get a new handle to the allocator when the arena is moved or copied.
         */
        static Buddy *getEmbedAtAlignment(u8 *main, u64 memSize, u64 alignment);

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
        Buddy *resize(u64 newSize);

        Buddy *resizeStandard(u64 newMemorySize);

        Buddy *resizeEmbedded(u64 newMemorySize);

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
        void *malloc(u64 requestedSize);

        /* Use the specified buddy to allocate zeroed memory. See calloc. */
        void *calloc(u64 membersCount, u64 membersSize);

        /* Realloc semantics are a joke. See realloc. */
        void *reAlloc(void *ptr, u64 requestedSize, bool ignoreData);

        /* Realloc-like behavior that checks for overflow. See reAllocArray */
        void *reAllocArray(void *ptr, u64 membersCount, u64 membersSize, bool ignoreData);

        /* Use the specified buddy to free memory. See free. */
        void free(void *ptr);

        /* Tests if the arena can be shrunk in half */
        bool canShrink();

        /* Tests if the arena is completely empty */
        bool isEmpty();

        /* Tests if the arena is completely full */
        bool isFull();

        bool isFree(u64 from);

        u64 getArenaSize() const;

        /*
         * Reports the arena's free size. Note that this is (often) not a continuous size
         * but the sum of all free slots in the buddy.
         */
        u64 getArenaFreeSize();

        /*
         * Reservation functions
         */

        /* A (safer) free with a size. Will not free unless the size fits the target span. */
        BuddySafeFreeStatus buddySafeFree(void *ptr, u64 requestedSize);

        /* Reserve a range by marking it as allocated. Useful for dealing with physical memory. */
        void reserveRange(void *ptr, u64 requestedSize);

        /* Release a reserved memory range. Unsafe, this can mess up other allocations if called with wrong parameters! */
        void unsafeReleaseRange(void *ptr, u64 requestedSize);

        /*
         * Iteration functions
         */

        /*
         * Iterate through the free and allocated slots and call the provided function for each of them.
         *
         * If the provided function returns a non-NULL result, the iteration stops and the result
         * is returned to call. NULL is returned upon completing iteration without stopping.
         *
         * The iteration order is implementation-defined and may change between versions.
         */
        void *walk(void *(fp)(void *ctx, void *addr, u64 slotSize, u64 allocated), void *ctx);

        /*
         * Miscellaneous functions
         */

        /*
         * Calculates the fragmentation in the allocator in a 0-255 range.
         * NOTE: if you are using a non-power-of-two sized arena, the maximum upper bound can be lowed.
         */
        u8 fragmentation();

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
        void enableChangeTracking(void *ctx, void (*tracker)(void *, u8 *, u64));

        static u32 isValidAlignment(u64 alignment);

        u64 depthForSize(u64 requestedSize) const;

        u64 sizeForDepth(u64 depth) const;

        u8 *addressForPosition(BuddyTreePos pos);

        BuddyTreePos positionForAddress(const u8 *addr);

        u8* main();

        u32 relativeMode() const;

        BuddyTree *tree();

        u64 effectiveMemorySize() const;

        u64 virtualSlots() const;

        void toggleVirtualSlots(u32 state);

        void toggleRangeReservation(void *ptr, u64 requestedSize, u32 state);

        static BuddyEmbedCheck embedOffset(u64 memorySize, u64 alignment);

        BuddyTreePos deepestPositionForOffset(u64 offset);

        /*
         * Debug functions
         */

        /* Implementation defined */
        void debug();

    private:
        u64 memorySize {};
        u64 alignment {};

        union {
            u8 *main;
            ptrDiff mainOffset;
        } arena {};

        u64 buddyFlags {};
    };

    /*
     * A buddy allocation tree
     */
    class BuddyTree {
    public:
        /*
         * Initialization functions
         */

        /* Initializes a buddy allocation tree at the specified location */
        static BuddyTree *treeInit(u8 *at, u8 order);

        /* Indicates whether this is a valid position for the tree */
        bool treeValid(BuddyTreePos pos);

        /* Returns the order of the specified buddy allocation tree */
        u8 treeOrder();

        /*
         * Resize the tree to the new order. When downsizing, the left subtree is picked.
         * Caller must ensure enough space for the new order.
         */
        void treeResize(u8 desiredPos);

        /* Returns the size of a buddy allocation tree of the desired order*/
        static u64 buddyTreeSizeOf(u8 order);

        /*
         * Allocation functions
         */

        /* Returns the free capacity at or underneath the indicated position */
        u64 treeStatus(BuddyTreePos pos);

        /* Marks the indicated position as allocated and propagates the change */
        void treeMark(BuddyTreePos pos);

        /* Marks the indicated position as free and propagates the change */
        BuddyTreeReleaseStatus treeRelease(BuddyTreePos pos);

        /* Returns a free position at the specified depth or an invalid position */
        BuddyTreePos treeFindFree(u8 depth);

        /* Tests if the indicated position is available for allocation */
        bool treeIsFree(BuddyTreePos pos);

        /* Tests if the tree can be shrunk in half */
        bool treeCanShrink();

        static u64 orderForMemory(u64 memorySize, u64 alignment);

        /*
         * Miscellaneous functions
         */

        /* Enable change tracking state for this tree. */
        void treeEnableChangeTracking();

        /* Get a pointer to the parent buddy struct */
        Buddy *treeBuddy();

        /*
         * Debug functions
         */

        /* Implementation defined */
        void treeDebug(BuddyTreePos pos, u64 startSize);

        /* Implementation defined */
        u32 treeCheckInvariant(BuddyTreePos pos);

        /* Report fragmentation in a 0-255 range */
        u8 treeFragmentation();

        /*
         * Navigation functions
         */

        /* Returns a position at the root of a buddy allocation tree */
        BuddyTreePos treeRoot();

        /* Returns the leftmost child node */
        BuddyTreePos treeLeftmostChild();

        /* Returns the tree depth of the indicated position */
        static u64 treeDepth(BuddyTreePos pos);

        /* Returns the left child node position. Does not check if that is a valid position */
        BuddyTreePos treeLeftChild(BuddyTreePos pos);

        /* Returns the right child node position. Does not check if that is a valid position */
        BuddyTreePos treeRightChild(BuddyTreePos pos);

        /* Returns the current sibling node position. Does not check if that is a valid position */
        BuddyTreePos treeSibling(BuddyTreePos pos);

        /* Returns the parent node position or an invalid position if there is no parent node */
        BuddyTreePos treeParent(BuddyTreePos pos);

        /* Returns the right adjacent node position or an invalid position if there is no right adjacent node */
        BuddyTreePos treeRightAdjacent(BuddyTreePos pos);

        /* Returns the at-depth index of the indicated position */
        static u64 treeIndex(BuddyTreePos pos);

        /* Return the interval of the deepest positions spanning the indicated position */
        BuddyTreeInterval treeInterval(BuddyTreePos pos);

        /* Checks if one interval contains another */
        bool treeIntervalContains(BuddyTreeInterval outer, BuddyTreeInterval inner);

        /* Return a walk state structure starting from the root of a tree */
        BuddyTreeWalkState treeWalkStateRoot();

        /* Walk the tree, keeping track in the provided state structure */
        u32 treeWalk(BuddyTreeWalkState *state);

    private:
        u64 upperPosBound {};
        u64 sizeForOrderOffset {};
        u8 order {};
        u8 flags {};

        /*
         * struct padding rules mean that there are
         * 16/48 bits available until the next increment
         */
    };
}

#endif