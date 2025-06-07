#include "VirtualAllocator.hpp"

#include "CommonMain.hpp"
#include "Math.hpp"
#include "PhysicalMemory.hpp"
#include "MainMemory.hpp"

extern limine_memmap_request memMapRequest;

namespace kernel::common::memory {
	// TODO: Change page flags to a class for multiarch
	AllocContext *VirtualAllocator::createContext(const bool isUserspace, const bool isProcess) {
		AllocContext *ctx = nullptr;

		if (isProcess) {
			ctx = new AllocContext();
		} else {
			ctx = reinterpret_cast<AllocContext *>(CommonMain::getInstance()->getPMM()->allocPages(1, true));
		}

		ctx->isUserspace = isUserspace;

		ctx->pageMap = PageMap();
		ctx->heapSize = pageSize;

		ctx->pageFlags = 0b00000011;

		if (isProcess) {
			if (isUserspace) {
				ctx->pageFlags |= 0b00000100;
			}

			ctx->heapStart = reinterpret_cast<u64 *>(pageSize);
		} else {
			ctx->heapStart = reinterpret_cast<u64 *>(alignUp<u64>(reinterpret_cast<u64>(&dataEnd), pageSize)) + pageSize;
		}

		ctx->blocks = reinterpret_cast<MemoryBlock *>(ctx->heapStart);

		ctx->pageMap.init(CommonMain::getInstance()->getPMM()->allocPages(1, true));

		memset(ctx->pageMap.getPageTable(), 0, pageSize);

		ctx->pageMap.mapPage(reinterpret_cast<u64>(ctx->heapStart), reinterpret_cast<u64>(CommonMain::getInstance()->getPMM()->allocPages(1, false)), ctx->pageFlags, false, false);

		return ctx;
	}

	void VirtualAllocator::initContext(AllocContext *ctx) {
		memset(ctx->heapStart, 0, pageSize);

		CommonMain::getTerminal()->debug("MemoryBlock size: %lu", "VirtualAllocator", sizeof(MemoryBlock));
		CommonMain::getTerminal()->debug("Heap start: 0x%.16lx", "VirtualAllocator", ctx->heapStart);

		ctx->blocks->size = ctx->heapSize - sizeof(MemoryBlock);
		ctx->freeSpace = ctx->blocks->size;
		ctx->blocks->free = true;
		ctx->blocks->next = nullptr;
	}

	u64 VirtualAllocator::getPhysicalAddress(const u64 virtualAddress) {
		const u64 alignedKernAddr = alignDown<u64>(virtualAddress, pageSize);
		const u64 diff = virtualAddress - alignedKernAddr;

		return CommonMain::getInstance()->getKernelAllocContext()->pageMap.getPhysAddress(alignedKernAddr) + diff;
	}

	// TODO: Maybe set to 0 too
	u64 *VirtualAllocator::alloc(AllocContext *ctx, const u64 size) {
		ctx->lock.lock();

		//CommonMain::getTerminal()->debugNF("Allocating %lu bytes", "VirtualAllocator", size);

		const u64 alignedSize = alignUp<u64>(size, sizeof(MemoryBlock));

		//CommonMain::getTerminal()->debugNF("Allocating %lu aligned bytes", "VirtualAllocator", alignedSize);
		//CommonMain::getTerminal()->debugNF("Free Space: %lu bytes", "VirtualAllocator", ctx->freeSpace);
		//CommonMain::getTerminal()->debugNF("Heap size: %lu bytes", "VirtualAllocator", ctx->heapSize);

		MemoryBlock* current = ctx->blocks;

		while (current != nullptr) {
			if (current->free and current->size >= alignedSize) {
				if (current->size > pageSize * 1000) {
					CommonMain::getTerminal()->error("AN 1 - Block 0x%.16lx is too big: %lu bytes", "VirtualAllocator", reinterpret_cast<u64>(current), current->size);
				}

				//CommonMain::getTerminal()->debugNF("AN 1 - Current: 0x%.16lx", "VirtualAllocator", reinterpret_cast<u64>(current));
				//CommonMain::getTerminal()->debugNF("AN 1 - Next: 0x%.16lx", "VirtualAllocator", reinterpret_cast<u64>(current->next));

				if (current->size >= alignedSize + sizeof(MemoryBlock) + minBlockSize) {
					auto* newBlock = reinterpret_cast<MemoryBlock *>(reinterpret_cast<u64>(current) + sizeof(MemoryBlock) + alignedSize);

					newBlock->size = current->size - alignedSize - sizeof(MemoryBlock);
					newBlock->free = true;
					newBlock->next = current->next;
					current->next = newBlock;

					//CommonMain::getTerminal()->debugNF("AN 1 - New: 0x%.16lx", "VirtualAllocator", reinterpret_cast<u64>(current->next));

					ctx->freeSpace -= sizeof(MemoryBlock);

					if (newBlock->size > pageSize * 1000) {
						CommonMain::getTerminal()->error("AN - Block 0x%.16lx is too big: %lu bytes", "VirtualAllocator", reinterpret_cast<u64>(current), current->size);
					}
				}

				current->free = false;
				current->size = alignedSize;

				if (current->size > pageSize * 1000) {
					CommonMain::getTerminal()->error("AC - Block 0x%.16lx is too big: %lu bytes", "VirtualAllocator", reinterpret_cast<u64>(current), current->size);
				}

				ctx->freeSpace -= alignedSize;

				ctx->lock.unlock();

				return reinterpret_cast<u64 *>(reinterpret_cast<u64>(current) + sizeof(MemoryBlock));
			}

			current = current->next;
		}

		if (CommonMain::getInstance()->getPMM()->getFreeMemory() < alignedSize + sizeof(MemoryBlock)) {
			ctx->lock.unlock();

			return nullptr;
		}

		growHeap(ctx, alignedSize);

		ctx->lock.unlock();

		return alloc(ctx, alignedSize);
	}

	// TODO: Maybe improve speed by defragging only the current block
	void VirtualAllocator::free(AllocContext *ctx, u64 *ptr) {
		if (ptr == nullptr) {
			return;
		}

		ctx->lock.lock();

		auto* block = reinterpret_cast<MemoryBlock *>(reinterpret_cast<u64>(ptr) - sizeof(MemoryBlock));
		block->free = true;

		memset(ptr, 0, block->size);

		ctx->freeSpace += block->size;

		//defrag(ctx);

		/*if (ctx->heapSize > pageSize) {
			shrinkHeap(ctx);
		}*/

		ctx->lock.unlock();
	}

	// Todo: Fix infinite loop
	void VirtualAllocator::defrag(AllocContext *ctx) {
		MemoryBlock* current = ctx->blocks;

		while (current != nullptr and current->next != nullptr) {
			if (current->free and current->next->free) {
				current->size += sizeof(MemoryBlock) + current->next->size;
				current->next = current->next->next;

				ctx->freeSpace += sizeof(MemoryBlock);
			} else {
				current = current->next;
			}
		}
	}

	void VirtualAllocator::growHeap(AllocContext *ctx, const u64 minSize) {
		const usize totalSize = minSize + sizeof(MemoryBlock);
		const auto allocSize = alignUp<usize>(totalSize, pageSize);

		auto baseAddress = reinterpret_cast<u64 *>(reinterpret_cast<u64>(ctx->heapStart) + ctx->heapSize);

		for (usize offset = 0; offset < allocSize; offset += pageSize) {
			u64 *newPage = CommonMain::getInstance()->getPMM()->allocPages(1, false);

			if (!newPage) {
				CommonMain::getTerminal()->error("Could not allocate a new page!", "VirtualAllocator");

				return;
			}

			ctx->pageMap.mapPage(reinterpret_cast<u64>(baseAddress) + offset, reinterpret_cast<u64>(newPage), ctx->pageFlags, !ctx->isUserspace, false);

			memset(reinterpret_cast<u64 *>(reinterpret_cast<u64>(baseAddress) + offset), 0, pageSize);
		}

		auto* newBlock = reinterpret_cast<MemoryBlock *>(baseAddress);
		newBlock->size = allocSize - sizeof(MemoryBlock);
		newBlock->free = true;
		newBlock->next = nullptr;

		MemoryBlock* last = ctx->blocks;

		while (last->next) {
			last = last->next;
		}

		last->next = newBlock;

		ctx->heapSize += allocSize;
		ctx->freeSpace += newBlock->size;

		defrag(ctx);
	}

	// TODO: Fix
	void VirtualAllocator::shrinkHeap(AllocContext *ctx) {
		MemoryBlock* current = ctx->blocks;
		MemoryBlock* prev = nullptr;

		while (current and current->next) {
			prev = current;
			current = current->next;
		}

		if (not current or not current->free or current->size < pageSize) {
			return;
		}

		const u64 blockStart = reinterpret_cast<u64>(current);
		const u64 heapEnd = reinterpret_cast<u64>(ctx->heapStart) + ctx->heapSize;

		const usize totalSize = sizeof(MemoryBlock) + current->size;
		const usize pagesToFree = totalSize / pageSize;

		if (pagesToFree * pageSize != totalSize) {
			return;
		}

		for (usize i = 0; i < pagesToFree * pageSize; i += pageSize) {
			const auto virtAddress = reinterpret_cast<u64 *>(blockStart + i);

			CommonMain::getInstance()->getPMM()->freePages(virtAddress, 1);
			ctx->pageMap.unMapPage(*virtAddress);
		}

		if (prev) {
			prev->next = nullptr;
		} else {
			ctx->blocks = nullptr;
		}

		ctx->heapSize -= pagesToFree * pageSize;

		ctx->freeSpace -= pagesToFree * pageSize;
	}

	void VirtualPageAllocator::init(const u64 kernAddr) {
		CommonMain::getTerminal()->debug("Heap size: %lu", "VirtualAllocator", CommonMain::getInstance()->getKernelAllocContext()->heapSize);

		const limine_memmap_entry *lastEntry = memMapRequest.response->entries[memMapRequest.response->entry_count - 1];

		CommonMain::getTerminal()->debug("Last entry addr: 0x%.16lx, end: 0x%.16lx", "VirtualAllocator", lastEntry->base, lastEntry->base + lastEntry->length);
		CommonMain::getTerminal()->debug("Kernel addr: 0x%.16lx", "VirtualAllocator", kernAddr);

		this->vPagesListPtr = new VpaListEntry();

		this->vPagesListPtr->base = lastEntry->base + lastEntry->length + pageSize + CommonMain::getCurrentHhdm();

		this->vPagesListPtr->count = alignDown<u64>(kernAddr - pageSize - this->vPagesListPtr->base, pageSize) / pageSize;
	}

	u64 *VirtualPageAllocator::allocVPages(const u64 amount) const {
		VpaListEntry *currEntry = this->vPagesListPtr;

		while (currEntry != nullptr) {
			if (currEntry->count > amount and not currEntry->isAllocated) {
				currEntry->isAllocated = true;

				auto *newEntry = new VpaListEntry();

				newEntry->base = currEntry->base + (amount * pageSize);

				newEntry->count = currEntry->count - amount;
				currEntry->count = amount;

				newEntry->next = currEntry->next;
				newEntry->prev = currEntry;

				currEntry->next = newEntry;

				return reinterpret_cast<u64 *>(currEntry->base);
			}

			if (currEntry->count == amount and not currEntry->isAllocated) {
				currEntry->isAllocated = true;

				return reinterpret_cast<u64 *>(currEntry->base);
			}

			currEntry = currEntry->next;
		}

		return nullptr;
	}

	void VirtualPageAllocator::freeVPages(const u64 *addr) const {
		VpaListEntry *currEntry = this->vPagesListPtr;

		while (currEntry != nullptr) {
			if (currEntry->base == reinterpret_cast<u64>(addr)) {
				break;
			}

			currEntry = currEntry->next;
		}

		if (currEntry == nullptr) {
			return;
		}

		currEntry->isAllocated = false;

		if (currEntry->prev != nullptr and not currEntry->prev->isAllocated) {
			currEntry->prev->count += currEntry->count;

			const VpaListEntry *tmpEntry = currEntry;

			currEntry->prev->next = currEntry->next;

			if (currEntry->next != nullptr) {
				currEntry->next->prev = currEntry->prev;
			}

			currEntry = currEntry->prev;

			delete tmpEntry;
		}

		if (currEntry->next != nullptr and not currEntry->next->isAllocated) {
			currEntry->count += currEntry->next->count;

			currEntry->next = currEntry->next->next;

			if (currEntry->next != nullptr) {
				currEntry->next->prev = currEntry;
			}

			delete currEntry->next;
		}
	}

	// New Allocator

	Buddy *Buddy::init(u8 *at, u8 *main, const u64 memSize) {
		return initAlignment(at, main, memSize, BUDDY_ALLOC_ALIGN);
	}

	Buddy *Buddy::initAlignment(u8 *at, u8 *main, u64 memSize, const u64 alignment) {
		if (at == nullptr or main == nullptr or at == main) {
			return nullptr;
		}

		if (not isValidAlignment(alignment)) {
			return nullptr;
		}

		if ((reinterpret_cast<uPtr>(at) % BUDDY_ALIGNOF(Buddy)) != 0) {
			return nullptr;
		}

		if ((reinterpret_cast<uPtr>(main) % BUDDY_ALIGNOF(u64)) != 0) {
			return nullptr;
		}

		/* Trim down memory to alignment */
		if (memSize % alignment) {
			memSize -= memSize % alignment;
		}

		if (buddySizeOfAlignment(memSize, alignment) == 0) {
			return nullptr;
		}

		const u64 buddyTreeOrder = BuddyTree::orderForMemory(memSize, alignment);

		const auto buddy = reinterpret_cast<Buddy *>(at);

		buddy->arena.main = main;
		buddy->memorySize = memSize;
		buddy->buddyFlags = 0;
		buddy->alignment = alignment;

		BuddyTree::treeInit(reinterpret_cast<u8 *>(buddy) + sizeof(*buddy), static_cast<u8>(buddyTreeOrder));

		buddy->toggleVirtualSlots(1);

		return buddy;
	}

    Buddy *Buddy::initEmbed(u8 *main, const u64 memSize) {
		return initEmbedAlignment(main, memSize, BUDDY_ALLOC_ALIGN);
	}

    Buddy *Buddy::initEmbedAlignment(u8 *main, const u64 memSize, const u64 alignment) {
		if (main == nullptr) {
			return nullptr;
		}

		if (not isValidAlignment(alignment)) {
			return nullptr;
		}

		const BuddyEmbedCheck checkResult = embedOffset(memSize, alignment);

		if (checkResult.canFit == 0) {
			return nullptr;
		}

		Buddy *buddy = initAlignment(reinterpret_cast<u8 *>(reinterpret_cast<u64>(main) + checkResult.offset), main, checkResult.offset, alignment);

		if (buddy == nullptr) {
			return nullptr;
		}

		buddy->buddyFlags |= buddyRelativeMode;
		buddy->arena.mainOffset = static_cast<ptrDiff>(reinterpret_cast<u64>(buddy) - reinterpret_cast<u64>(main));

		return buddy;
	}

	Buddy *Buddy::getEmbedAt(u8 *main, const u64 memSize) {
		return getEmbedAtAlignment(main, memSize, BUDDY_ALLOC_ALIGN);
	}

	Buddy *Buddy::getEmbedAtAlignment(u8 *main, const u64 memSize, const u64 alignment) {
		const BuddyEmbedCheck checkResult = embedOffset(memSize, alignment);

		if (checkResult.canFit == 0) {
			return nullptr;
		}

		return reinterpret_cast<Buddy *>(reinterpret_cast<u64>(main) + checkResult.offset);
	}

    Buddy *Buddy::resize(const u64 newSize) {
		if (this->memorySize == newSize) {
			return this;
		}

		if (this->relativeMode()) {
			return this->resizeEmbedded(newSize);
		}

		return this->resizeStandard(newSize);
	}

    Buddy *Buddy::resizeStandard(u64 newMemorySize) {
		/* Trim down memory to alignment */
		if (newMemorySize % this->alignment) {
			newMemorySize -= newMemorySize % this->alignment;
		}

		if (not isFree(newMemorySize)) {
			return nullptr;
		}

		/* Release the virtual slots */
		this->toggleVirtualSlots(0);

		/* Calculate new tree order and resize it */
		const u64 treeOrder = BuddyTree::orderForMemory(newMemorySize, this->alignment);
		this->tree()->treeResize(static_cast<u8>(treeOrder));

		/* Store the new memory size and reconstruct any virtual slots */
		this->memorySize = newMemorySize;
		this->toggleVirtualSlots(1);

		/* Resize successful */
		return this;
	}

	Buddy *Buddy::resizeEmbedded(const u64 newMemorySize) {
		if (this == nullptr) {
			return nullptr;
		}

		auto [canFit, offset, buddySize] = embedOffset(newMemorySize, this->alignment);

		/* Ensure that the embedded allocator can fit */
		if (canFit == 0) {
			return nullptr;
		}

		/* Resize the allocator in the normal way */
		const Buddy *resized = this->resizeStandard(offset);

		if (resized == nullptr) {
			return nullptr;
		}

		/* Get the absolute main address. The relative will be invalid after relocation. */
		u8 *buddyMain = this->main();

		/* Relocate the allocator */
		auto *buddyDestination = reinterpret_cast<u8 *>(reinterpret_cast<u64>(buddyMain) + offset);
		memmove(buddyDestination, resized, buddySize);

		/* Update the main offset in the allocator */
		auto *relocated = reinterpret_cast<Buddy *>(buddyDestination);
		relocated->arena.mainOffset = static_cast<ptrDiff>(reinterpret_cast<u64>(buddyDestination) -  reinterpret_cast<u64>(buddyMain));

		return relocated;
	}

    u64 Buddy::buddySizeOf(const u64 memSize) {
		return buddySizeOfAlignment(memSize, BUDDY_ALLOC_ALIGN);
	}

    u64 Buddy::buddySizeOfAlignment(const u64 memSize, const u64 alignment) {
		if (not isValidAlignment(alignment)) {
			return 0;
		}

		if (memSize < alignment) {
			return 0;
		}

		const u64 treeOrder = BuddyTree::orderForMemory(memSize, alignment);

		return sizeof(Buddy) + BuddyTree::buddyTreeSizeOf(static_cast<u8>(treeOrder));
	}

    void *Buddy::malloc(u64 requestedSize) {
		if (this == nullptr) {
			return nullptr;
		}

		if (requestedSize == 0) {
			/*
			 * Batshit crazy code exists that calls malloc(0) and expects
			 * a result that can be safely passed to free().
			 * And even though this allocator will safely handle a free(NULL)
			 * the particular batshit code will expect a non-NULL malloc(0) result!
			 *
			 * See also https://wiki.sei.cmu.edu/confluence/display/c/MEM04-C.+Beware+of+zero-length+allocations
			 */

			requestedSize = 1;
		}

		if (requestedSize > this->memorySize) {
			return nullptr;
		}

		const u64 targetDepth = this->depthForSize(requestedSize);
		BuddyTree *tree = this->tree();
		const BuddyTreePos pos = tree->treeFindFree(static_cast<u8>(targetDepth));

		if (not tree->treeValid(pos)) {
			return nullptr; /* No slot found */
		}

		/* Allocate the slot */
		tree->treeMark(pos);

		/* Find and return the actual memory address */
		return this->addressForPosition(pos);
	}

    void *Buddy::calloc(u64 membersCount, u64 membersSize) {
		if (membersCount == 0 or membersSize == 0) {
			/* See the gleeful remark in malloc */
			membersCount = 1;
			membersSize = 1;
		}

		/* Check for overflow */
		if (((membersCount * membersSize) / membersCount) != membersSize) {
			return nullptr;
		}

		const u64 totalSize = membersCount * membersSize;
		void *result = this->malloc(totalSize);

		if (result != nullptr) {
			memset(result, 0, totalSize);
		}

		return result;
	}

    void *Buddy::reAlloc(void *ptr, const u64 requestedSize, const bool ignoreData) {
		/*
		 * reAlloc is a joke:
		 * - nullptr degrades into malloc
		 * - Zero size degrades into free
		 * - Same size as previous malloc/calloc/realloc is a no-op or a relocation
		 * - Smaller size than previous *alloc decrease the allocated size with an optional relocation
		 * - If the new allocation cannot be satisfied, nullptr is returned BUT the slot is preserved
		 * - Larger size than previous *alloc increase tha allocated size with an optional relocation
		 */
		if (ptr == nullptr) {
			return this->malloc(requestedSize);
		}

		if (requestedSize == 0) {
			this->free(ptr);

			return nullptr;
		}

		if (requestedSize > this->memorySize) {
			return nullptr;
		}

		/* Find the position tracking this address */
		BuddyTree *tree = this->tree();
		const BuddyTreePos origin = this->positionForAddress(static_cast<u8 *>(ptr));

		if (not tree->treeValid(origin)) {
			return nullptr;
		}

		const u64 currDepth = BuddyTree::treeDepth(origin);
		const u64 targetDepth = this->depthForSize(requestedSize);

		/* Release the position and perform a search */
		tree->treeRelease(origin);

		const BuddyTreePos newPos = tree->treeFindFree(static_cast<u8>(targetDepth));

		if (not tree->treeValid(newPos)) {
			/* allocation failure, restore mark and return null */
			tree->treeMark(origin);

			return nullptr;
		}

		if (origin.index == newPos.index) {
			/* Allocated to the same slot, restore mark and return null */
			tree->treeMark(origin);

			return ptr;
		}

		void *dest = this->addressForPosition(newPos);

		if (not ignoreData) {
			/* Copy the content */
			const void *source = this->addressForPosition(origin);

			memmove(dest, source, this->sizeForDepth(currDepth > targetDepth ? currDepth : targetDepth));
		}

		/* Allocate and return */
		tree->treeMark(newPos);

		return dest;
	}

    void *Buddy::reAllocArray(void *ptr, const u64 membersCount, const u64 membersSize, const bool ignoreData) {
		if (membersCount == 0 or membersSize == 0) {
			return this->reAlloc(ptr, 0, ignoreData);
		}

		/* Check for overflow */
		if (((membersCount * membersSize) / membersCount) != membersSize) {
			return nullptr;
		}

		return this->reAlloc(ptr, membersCount * membersSize, ignoreData);
	}

    void Buddy::free(void *ptr) {
		if (this == nullptr) {
			return;
		}

		if (ptr == nullptr) {
			return;
		}

		auto *dst = static_cast<u8 *>(ptr);

		if (u8 *main = this->main(); (dst < main) or (reinterpret_cast<u64>(dst) >= (reinterpret_cast<u64>(main) + this->memorySize))) {
			return;
		}

		/* Find the position tracking this address */
		BuddyTree *tree = this->tree();
		const BuddyTreePos pos = this->positionForAddress(dst);

		if (not tree->treeValid(pos)) {
			return;
		}

		/* Release the position */
		tree->treeRelease(pos);
	}

    bool Buddy::canShrink() {
		if (this == nullptr) {
			return false;
		}

		return this->isFree(this->memorySize / 2);
	}

    bool Buddy::isEmpty() {
		if (this == nullptr) {
			return false;
		}

		return this->isFree(0);
	}

    bool Buddy::isFull() {
		if (this == nullptr) {
			return false;
		}

		BuddyTree *tree = this->tree();
		const BuddyTreePos pos = tree->treeRoot();

		return tree->treeStatus(pos) == tree->treeOrder();
	}

	/*
	 * Internal function that checks if there are any allocations
	 * after the indicated relative memory index. Used to check if
	 * the arena can be downsized.
	 * The from argument is already adjusted for alignment by caller
	 */
	bool Buddy::isFree(const u64 from) {
		const u64 effectiveMemorySize = this->effectiveMemorySize();
		const u64 virtualSlots = this->virtualSlots();

		const u64 to = effectiveMemorySize - ((virtualSlots ? (virtualSlots + 1) : 1) * this->alignment);

		BuddyTree *tree = this->tree();

		const BuddyTreeInterval queryRange = {
			.from = this->deepestPositionForOffset(from),
			.to = this->deepestPositionForOffset(to),
		};

		BuddyTreePos pos = this->deepestPositionForOffset(from);

		while (tree->treeValid(pos) and (pos.index < queryRange.to.index)) {
			BuddyTreeInterval currentTestRange = tree->treeInterval(pos);
			BuddyTreeInterval parentTestRange = tree->treeInterval(tree->treeParent(pos));

			while (tree->treeIntervalContains(queryRange, parentTestRange)) {
				pos = tree->treeParent(pos);
				currentTestRange = parentTestRange;
				parentTestRange = tree->treeInterval(tree->treeParent(pos));
			}

			/* Pos is now tracking an overlapping segment */
			if (not tree->treeIsFree(pos)) {
				return false;
			}

			/* Advance check */
			pos = tree->treeRightAdjacent(currentTestRange.to);
		}

		return true;
	}

	u64 Buddy::getArenaSize() const {
		if (this == nullptr) {
			return 0;
		}

		return this->memorySize;
	}

	u64 Buddy::getArenaFreeSize() {
		u64 result = 0;

		BuddyTree *tree = this->tree();

		const u64 treeOrder = tree->treeOrder();

		BuddyTreeWalkState state = tree->treeWalkStateRoot();

		do {
			if (const u64 posStatus = tree->treeStatus(state.currentPos); posStatus == (treeOrder - state.currentPos.depth + 1)) { /* Fully-allocated */
				state.goingUp = 1;
			} else if (posStatus == 0) { /* Free */
				state.goingUp = 1;

				result += this->sizeForDepth(state.currentPos.depth);
			}
		} while (tree->treeWalk(&state));

		return result;
	}

    BuddySafeFreeStatus Buddy::buddySafeFree(void *ptr, u64 requestedSize) {
		if (this == nullptr) {
			return BUDDY_SAFE_FREE_BUDDY_IS_NULL;
		}

		if (ptr == nullptr) {
			return BUDDY_SAFE_FREE_INVALID_ADDRESS;
		}

		auto *dst = static_cast<u8 *>(ptr);

		if (u8 *main = this->main(); (dst < main) or (reinterpret_cast<u64>(dst) >= (reinterpret_cast<u64>(main) + this->memorySize))) {
			return BUDDY_SAFE_FREE_INVALID_ADDRESS;
		}

		/* Find an allocated position tracking this address */
		BuddyTree *tree = this->tree();
		const BuddyTreePos pos = this->positionForAddress(dst);

		if (not tree->treeValid(pos)) {
			return BUDDY_SAFE_FREE_INVALID_ADDRESS;
		}

		const u64 allocatedSizeForDepth = this->sizeForDepth(pos.depth);

		if (requestedSize < this->alignment) {
			requestedSize = this->alignment;
		}

		if (requestedSize > allocatedSizeForDepth) {
			return BUDDY_SAFE_FREE_SIZE_MISMATCH;
		}

		if (requestedSize <= (allocatedSizeForDepth / 2)) {
			return BUDDY_SAFE_FREE_SIZE_MISMATCH;
		}

		/* Release the position */
		switch (tree->treeRelease(pos)) {
			case BUDDY_TREE_RELEASE_FAIL_PARTIALLY_USED:
				return BUDDY_SAFE_FREE_INVALID_ADDRESS;

			case BUDDY_TREE_RELEASE_SUCCESS:
				break;
		}

		return BUDDY_SAFE_FREE_SUCCESS;
	}

    void Buddy::reserveRange(void *ptr, const u64 requestedSize) {
		this->toggleRangeReservation(ptr, requestedSize, 1);
	}

    void Buddy::unsafeReleaseRange(void *ptr, const u64 requestedSize) {
		this->toggleRangeReservation(ptr, requestedSize, 0);
	}

    void *Buddy::walk(void *(fp)(void *ctx, void *addr, u64 slotSize, u64 allocated), void *ctx) {
		if (this == nullptr) {
			return nullptr;
		}

		if (fp == nullptr) {
			return nullptr;
		}

		u8 *main = this->main();
		const u64 effectiveMemorySize = this->effectiveMemorySize();
		BuddyTree *tree = this->tree();
		const u64 treeOrder = tree->treeOrder();

		BuddyTreeWalkState state = tree->treeWalkStateRoot();

		do {
			const u64 posStatus = tree->treeStatus(state.currentPos);

			/* Partially-allocated */
			if (posStatus != (treeOrder - state.currentPos.depth + 1)) {
				continue;
			}

			/*
			 * The tree doesn't make a distinction of a fully allocated node
			 *  due to a single allocation and a fully allocated due to maxed out
			 *  child allocations - we need to check the children.
			 * A child-allocated node will have both children set to their maximum,
			 *  but it is enough to check just one for non-zero.
			 */
			if (const BuddyTreePos testPos = tree->treeLeftChild(state.currentPos); tree->treeValid(testPos) and tree->treeStatus(testPos)) {
				continue;
			}

			/* The current node is free or allocated, process */
			const u64 posSize = effectiveMemorySize >> (state.currentPos.depth - 1u);
			u8 *addr = this->addressForPosition(state.currentPos);

			if ((reinterpret_cast<u64>(addr) - reinterpret_cast<u64>(main) + posSize) > this->memorySize) {
				/*
				 * Do not process virtual slots
				 * As virtual slots are on the right side of the tree
				 *  if we see a one with the current iteration order this
				 *  means that all later slots will be virtual,
				 *  hence we can return early.
				 */
				return nullptr;
			}

			if (void *callbackResult = (fp)(ctx, addr, posSize, posStatus > 0); callbackResult != nullptr) {
				return callbackResult;
			}

			state.goingUp = 1;
		} while (tree->treeWalk(&state));

		return nullptr;
	}

    u8 Buddy::fragmentation() {
		if (this == nullptr) {
			return 0;
		}

		return this->tree()->treeFragmentation();
	}

    void Buddy::enableChangeTracking(void *ctx, void (*tracker)(void *, u8 *, u64)) {
		BuddyTree *tree = this->tree();

		auto *header = reinterpret_cast<BuddyChangeTracker *>(this->main());

		/* Allocate memory for the change tracking header */
		this->reserveRange(this->main(), sizeof(BuddyChangeTracker));

		/* Fill in the change tracking header */
		header->ctx = ctx;
		header->tracker = tracker;

		/* Indicate that the tree should perform change tracking */
		tree->treeEnableChangeTracking();
	}

	u32 Buddy::isValidAlignment(const u64 alignment) {
		return ceilingPow2(alignment) == alignment;
	}

	u64 Buddy::depthForSize(u64 requestedSize) const {
		if (requestedSize < this->alignment) {
			requestedSize = this->alignment;
		}

		u64 depth = 1;
		u64 effectiveMemorySize = this->effectiveMemorySize();

		while ((effectiveMemorySize / requestedSize) >> 1u) {
			depth++;

			effectiveMemorySize >>= 1u;
		}

		return depth;
	}

	u64 Buddy::sizeForDepth(const u64 depth) const {
		return ceilingPow2(this->memorySize) >> (depth - 1);
	}

	u8 *Buddy::addressForPosition(const BuddyTreePos pos) {
		const u64 blockSize = this->sizeForDepth(BuddyTree::treeDepth(pos));
		const u64 addr = blockSize * BuddyTree::treeIndex(pos);

		return reinterpret_cast<u8 *>(reinterpret_cast<u64>(this->main()) + addr);
	}

	BuddyTreePos Buddy::positionForAddress(const u8 *addr) {
		u8 *main = this->main();
		const u64 offset = reinterpret_cast<u64>(addr) - reinterpret_cast<u64>(main);

		if (offset % this->alignment) {
			/* Invalid alignment */
			return INVALID_POS;
		}

		BuddyTree *tree = this->tree();
		BuddyTreePos pos = this->deepestPositionForOffset(offset);

		/* Find the actual allocated position tracking this address */
		while (not tree->treeStatus(pos)) {
			pos = tree->treeParent(pos);

			if (not tree->treeValid(pos)) {
				return INVALID_POS;
			}
		}

		if (this->addressForPosition(pos) != addr) {
			/* Invalid alignment */
			return INVALID_POS;
		}

		return pos;
	}

	u8* Buddy::main() {
		if (this->relativeMode()) {
			return reinterpret_cast<u8 *>(reinterpret_cast<u64>(this) - this->arena.mainOffset);
		}

		return this->arena.main;
	}

	u32 Buddy::relativeMode() const {
		return static_cast<u32>(this->buddyFlags & buddyRelativeMode);
	}

	BuddyTree *Buddy::tree() {
		return reinterpret_cast<BuddyTree *>(reinterpret_cast<u64>(this) + sizeof(*this));
	}

	u64 Buddy::effectiveMemorySize() const {
		return ceilingPow2(this->memorySize);
	}

	u64 Buddy::virtualSlots() const {
		const u64 memorySize = this->memorySize;
		const u64 effectiveMemorySize = this->effectiveMemorySize();

		if (effectiveMemorySize == memorySize) {
			return 0;
		}

		return (effectiveMemorySize - memorySize) / this->alignment;
	}

	void Buddy::toggleVirtualSlots(const u32 state) {
		const u64 memorySize = this->memorySize;

		/* Mask/unmask the virtual space if memory is not a power of two */
		const u64 effectiveMemorySize = this->effectiveMemorySize();

		if (effectiveMemorySize == memorySize) {
			return;
		}

		/* Get the area that we need to mask and pad it to alignment */
		/* Node memory size is already aligned to buddy->alignment */
		u64 delta = effectiveMemorySize - memorySize;

		BuddyTree *tree = this->tree();
		BuddyTreePos pos = tree->treeRightChild(tree->treeRoot());

		while (delta) {
			const u64 currentPosSize = this->sizeForDepth(BuddyTree::treeDepth(pos));

			if (delta == currentPosSize) {
				/* Toggle current pos */
				if (state) {
					tree->treeMark(pos);
				} else {
					tree->treeRelease(pos);
				}

				break;
			}

			if (delta <= (currentPosSize / 2)) {
				/* Re-run for right child */
				pos = tree->treeRightChild(pos);

				continue;
			} else {
				/* Toggle right child */
				if (state) {
					tree->treeMark(tree->treeRightChild(pos));
				} else {
					tree->treeRelease(tree->treeRightChild(pos));
				}

				/* Reduce delta */
				delta -= currentPosSize / 2;

				/* Re-run for left child */
				pos = tree->treeLeftChild(pos);

				continue;
			}
		}
	}

	void Buddy::toggleRangeReservation(void *ptr, u64 requestedSize, const u32 state) {
		if (this == nullptr) {
			return;
		}

		if (ptr == nullptr) {
			return;
		}

		if (requestedSize == 0) {
			return;
		}

		auto *dst = static_cast<u8 *>(ptr);
		u8 *main = this->main();

		if ((dst < main) or (reinterpret_cast<u64>(dst) >= (reinterpret_cast<u64>(main) + this->memorySize))) {
			return;
		}

		/* Find the deepest position tracking this address */
		BuddyTree *tree = this->tree();

		const u64 offset = reinterpret_cast<u64>(dst) - reinterpret_cast<u64>(main);

		BuddyTreePos pos = this->deepestPositionForOffset(offset);

		/* Advance one position at a time and process */
		while (requestedSize) {
			if (state) {
				tree->treeMark(pos);
			} else {
				tree->treeRelease(pos);
			}

			requestedSize = (requestedSize < this->alignment) ? 0 : (requestedSize - this->alignment);

			pos.index++;
		}
	}

	BuddyEmbedCheck Buddy::embedOffset(const u64 memorySize, const u64 alignment) {
		BuddyEmbedCheck checkResult = {};

		memset(&checkResult, 0, sizeof(checkResult));

		checkResult.canFit = 1;
		u64 buddySize = buddySizeOfAlignment(memorySize, alignment);

		if (buddySize >= memorySize) {
			checkResult.canFit = 0;
		}

		u64 offset = memorySize - buddySize;

		if (offset % BUDDY_ALIGNOF(Buddy) != 0) {
			buddySize += offset % BUDDY_ALIGNOF(Buddy);

			if (buddySize >= memorySize) {
				checkResult.canFit = 0;
			}

			offset = memorySize - buddySize;
		}

		if (checkResult.canFit) {
			checkResult.offset = offset;
			checkResult.buddySize = buddySize;
		}

		return checkResult;
	}

	BuddyTreePos Buddy::deepestPositionForOffset(const u64 offset) {
		const u64 index = offset / this->alignment;

		BuddyTreePos pos = this->tree()->treeLeftmostChild();

		pos.index += index;

		return pos;
	}

    void Buddy::debug() {
		Terminal *terminal = CommonMain::getTerminal();

		terminal->debug("Buddy Allocator Info:", "Buddy");
		terminal->debug("	Location: %lp", "Buddy", this);
		terminal->debug("	Arena: %lp", "Buddy", this->main());
		terminal->debug("	Memory size: %lu", "Buddy", this->memorySize);

		if (this->relativeMode()) {
			terminal->debug("	Mode: Embedded", "Buddy");
		} else {
			terminal->debug("	Mode: Standard", "Buddy");
		}

		terminal->debug("	Virtual slots: %lu", "Buddy", this->virtualSlots());
		terminal->debug("	Allocator tree follows:", "Buddy");

		BuddyTree *tree = this->tree();

		tree->treeDebug(tree->treeRoot(), this->effectiveMemorySize());
    }



	BuddyTree *BuddyTree::treeInit(u8 *at, u8 order) {

	}

	bool BuddyTree::treeValid(BuddyTreePos pos) {

	}

	u8 BuddyTree::treeOrder() {

	}

    void BuddyTree::treeResize(u8 desiredPos) {

	}

    u64 BuddyTree::buddyTreeSizeOf(u8 order) {

	}

    u64 BuddyTree::treeStatus(BuddyTreePos pos) {

	}

    void BuddyTree::treeMark(BuddyTreePos pos) {

	}

    BuddyTreeReleaseStatus BuddyTree::treeRelease(BuddyTreePos pos) {

	}

    BuddyTreePos BuddyTree::treeFindFree(u8 depth) {

	}

    bool BuddyTree::treeIsFree(BuddyTreePos pos) {

	}

    bool BuddyTree::treeCanShrink() {

	}

	u64 BuddyTree::orderForMemory(const u64 memorySize, const u64 alignment) {
		const u64 blocks = memorySize / alignment;

		return highestBitPosition(ceilingPow2(blocks));
	}

    void BuddyTree::treeEnableChangeTracking() {

	}

    Buddy *BuddyTree::treeBuddy() {

	}

    void BuddyTree::treeDebug(BuddyTreePos pos, u64 startSize) {

	}

    u32 BuddyTree::treeCheckInvariant(BuddyTreePos pos) {

	}

    u8 BuddyTree::treeFragmentation() {

	}

    BuddyTreePos BuddyTree::treeRoot() {

	}

    BuddyTreePos BuddyTree::treeLeftmostChild() {

	}

    u64 BuddyTree::treeDepth(BuddyTreePos pos) {

	}

    BuddyTreePos BuddyTree::treeLeftChild(BuddyTreePos pos) {

	}

     BuddyTreePos BuddyTree::treeRightChild(BuddyTreePos pos) {

     }

    BuddyTreePos BuddyTree::treeSibling(BuddyTreePos pos) {

    }

	BuddyTreePos BuddyTree::treeParent(BuddyTreePos pos) {

	}

    BuddyTreePos BuddyTree::treeRightAdjacent(BuddyTreePos pos) {

    }

    u64 BuddyTree::treeIndex(BuddyTreePos pos) {

    }

    BuddyTreeInterval BuddyTree::treeInterval(BuddyTreePos pos) {

    }

    bool BuddyTree::treeIntervalContains(BuddyTreeInterval outer, BuddyTreeInterval inner) {

    }

    BuddyTreeWalkState BuddyTree::treeWalkStateRoot() {

    }

	u32 BuddyTree::treeWalk(BuddyTreeWalkState *state) {

	}
}