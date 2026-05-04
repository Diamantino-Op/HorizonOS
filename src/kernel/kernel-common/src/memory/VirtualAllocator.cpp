#include "VirtualAllocator.hpp"

#include "CommonMain.hpp"
#include "Math.hpp"
#include "PhysicalMemory.hpp"
#include "MainMemory.hpp"

extern limine_memmap_request memMapRequest;
extern limine_paging_mode_request pagingModeRequest;

namespace kernel::common::memory {
	// TODO: Change page flags to a class for multi arch
	AllocContext *VirtualAllocator::createContext() {
		AllocContext *ctx = nullptr;

		const u64 kernelEnd = alignUp<u64>(reinterpret_cast<u64>(&dataEnd), pageSize);
		const u64 ctxPage = reinterpret_cast<u64>(CommonMain::getInstance()->getPMM()->allocPages(1, true));

		ctx = reinterpret_cast<AllocContext *>(ctxPage);

		ctx->isUserspace = false;
		ctx->heapSize = pageSize - sizeof(AllocContext);
		ctx->pageFlags = 0b00000011;

		ctx->heapStart = reinterpret_cast<u64 *>(kernelEnd + sizeof(AllocContext));
		ctx->blocks = reinterpret_cast<MemoryBlock *>(ctx->heapStart);

		u64 *newPageMap = CommonMain::getInstance()->getPMM()->allocPages(1, true);

		ctx->pageMap.init(newPageMap, reinterpret_cast<u64>(newPageMap) - CommonMain::getCurrentHhdm(), ctx, true);

		ctx->pageMap.mapPage(kernelEnd, ctxPage - CommonMain::getCurrentHhdm(), ctx->pageFlags, true, false);

		return ctx;
	}

	CreatedContext VirtualAllocator::createProcessContext() {
		const u64 tmpValue = startCreateArch();
		const u64 processAddr = getProcessAllocStart();

		CommonMain::getTerminal()->debug("Process Address: 0x%.16lx", "VirtualAllocator", processAddr + pageSize);

		const u64 ctxAddr = reinterpret_cast<u64>(CommonMain::getInstance()->getPMM()->allocPages(1, false));

		CommonMain::getInstance()->getKernelAllocContext()->pageMap.mapPage(processAddr + pageSize, ctxAddr, 0b00000011, false, false);

		auto *ctxKern = reinterpret_cast<AllocContext *>(ctxAddr + CommonMain::getCurrentHhdm());
		auto *ctx = reinterpret_cast<AllocContext *>(processAddr + pageSize);

		ctxKern->isUserspace = false;
		ctxKern->heapSize = pageSize - sizeof(AllocContext);
		ctxKern->pageFlags = 0b00000011;

		ctxKern->heapStart = reinterpret_cast<u64 *>(processAddr + pageSize + sizeof(AllocContext));
		ctxKern->blocks = reinterpret_cast<MemoryBlock *>(ctxKern->heapStart);

		const u64 pageMapAddr = reinterpret_cast<u64>(CommonMain::getInstance()->getPMM()->allocPages(1, false));

		CommonMain::getTerminal()->debug("PageMap Address: 0x%.16lx", "VirtualAllocator", pageMapAddr);

		CommonMain::getInstance()->getKernelAllocContext()->pageMap.mapPage(processAddr, pageMapAddr, ctx->pageFlags , false, false);

		ctxKern->pageMap.init(reinterpret_cast<u64 *>(processAddr), pageMapAddr, ctxKern, true); // , not ctx->isUserspace

		shareKernelPages(ctxKern);
		initContext(ctxKern);

		ctxKern->pageMap.mapPage(processAddr, pageMapAddr, ctxKern->pageFlags, false, false);
		ctxKern->pageMap.mapPage(processAddr + pageSize, ctxAddr, ctxKern->pageFlags, false, false);

		endCreateArch(tmpValue);

		return {
			.ctx = ctx,
			.ctkKern = reinterpret_cast<AllocContext *>(ctxAddr + CommonMain::getCurrentHhdm()),
		};
	}

	//TODO: Check why no work without adding the &
	u64 VirtualAllocator::getProcessAllocStart() {
		if (pagingModeRequest.response != nullptr and pagingModeRequest.response->mode == 1) {
			return ((CommonMain::getCurrentHhdm() - 0x1000000000000) & ~0xfe00000000000000);
		}

		return ((CommonMain::getCurrentHhdm() - 0x8000000000) & ~0xffff000000000000);
	}

	void VirtualAllocator::initContext(AllocContext *ctx) {
		memset(ctx->heapStart, 0, ctx->heapSize);

		for (auto &freeList : ctx->freeLists) {
			freeList = nullptr;
		}

		CommonMain::getTerminal()->debug("MemoryBlock size: %lu", "VirtualAllocator", sizeof(MemoryBlock));
		CommonMain::getTerminal()->debug("Heap start: 0x%.16lx", "VirtualAllocator", ctx->heapStart);

		ctx->blocks->size = ctx->heapSize - sizeof(MemoryBlock);
		ctx->freeSpace = ctx->blocks->size;
		ctx->blocks->free = true;
		ctx->blocks->next = nullptr;
		ctx->blocks->prev = nullptr;
		ctx->blocks->freeNext = nullptr;
		ctx->blocks->freePrev = nullptr;
		ctx->lastBlock = ctx->blocks;

		insertFreeList(ctx, ctx->blocks);
	}

	u64 VirtualAllocator::getPhysicalAddress(const u64 virtualAddress) {
		const u64 alignedKernAddr = alignDown<u64>(virtualAddress, pageSize);
		const u64 diff = virtualAddress - alignedKernAddr;

		return CommonMain::getInstance()->getKernelAllocContext()->pageMap.getPhysAddress(alignedKernAddr) + diff;
	}

	// TODO: Maybe set to 0 too
	// TODO: Use Linked List
	u64 *VirtualAllocator::alloc(AllocContext *ctx, const u64 size, const bool isUserAlloc) {
		const u64 alignedSize = alignUp<u64>(size, sizeof(MemoryBlock));

		const bool prevIF = ctx->lock.lock();

		allocStart:

		if (ctx->freeSpace < alignedSize + sizeof(MemoryBlock)) {
			growHeap(ctx, alignedSize + (sizeof(MemoryBlock) * 2), isUserAlloc);
		}

		const usize startClass = getSizeClassIndex(alignedSize);

		for (usize classIdx = startClass; classIdx < SIZE_CLASS_COUNT; classIdx++) {
			MemoryBlock* current = ctx->freeLists[classIdx];

			while (current != nullptr) {
				if (current->size >= alignedSize) {
					const usize originalSize = current->size;

					removeFreeList(ctx, current);

					if (originalSize >= alignedSize + sizeof(MemoryBlock) + minBlockSize) {
						auto* newBlock = reinterpret_cast<MemoryBlock *>(reinterpret_cast<u64>(current) + sizeof(MemoryBlock) + alignedSize);

						newBlock->size = originalSize - alignedSize - sizeof(MemoryBlock);
						newBlock->free = true;
						newBlock->next = current->next;
						newBlock->prev = current;
						newBlock->freeNext = nullptr;
						newBlock->freePrev = nullptr;

						if (newBlock->next != nullptr) {
							newBlock->next->prev = newBlock;
						} else {
							ctx->lastBlock = newBlock;
						}

						current->next = newBlock;
						current->size = alignedSize;

						ctx->freeSpace -= sizeof(MemoryBlock);
						ctx->freeSpace -= alignedSize;

						insertFreeList(ctx, newBlock);
					} else {
						ctx->freeSpace -= originalSize;
					}

					current->free = false;
					//current->size = alignedSize;

					//ctx->freeSpace -= alignedSize;

					ctx->lock.unlock(prevIF);

					return reinterpret_cast<u64 *>(reinterpret_cast<u64>(current) + sizeof(MemoryBlock));
				}

				current = current->freeNext;
			}
		}

		if (CommonMain::getInstance()->getPMM()->getFreeMemory() < alignedSize + sizeof(MemoryBlock)) {
			ctx->lock.unlock(prevIF);

			return nullptr;
		}

		growHeap(ctx, alignedSize, isUserAlloc);

		goto allocStart;
	}

	// TODO: Maybe improve speed by defragging only the current block
	void VirtualAllocator::free(AllocContext *ctx, u64 *ptr) {
		if (ptr == nullptr) {
			return;
		}

		const bool prevIF = ctx->lock.lock();

		auto* block = reinterpret_cast<MemoryBlock *>(reinterpret_cast<u64>(ptr) - sizeof(MemoryBlock));
		block->free = true;

		// TODO: Only do when freeing from user mem
		//memset(ptr, 0, block->size);

		ctx->freeSpace += block->size;

		defrag(ctx, block);

		/*if (ctx->heapSize > pageSize) {
			shrinkHeap(ctx);
		}*/

		ctx->lock.unlock(prevIF);
	}

	void VirtualAllocator::defrag(AllocContext *ctx, MemoryBlock *block) {
		if (block == nullptr) {
			return;
		}

		if (block->prev != nullptr and block->prev->free and areAdjacent(block->prev, block)) {
			removeFreeList(ctx, block->prev);

			block->prev->size += block->size + sizeof(MemoryBlock);
			block->prev->next = block->next;

			if (block->next != nullptr) {
				block->next->prev = block->prev;
			} else {
				ctx->lastBlock = block->prev;
			}

			block = block->prev;

			ctx->freeSpace += sizeof(MemoryBlock);
		}

		if (block->next != nullptr and block->next->free and areAdjacent(block, block->next)) {
			removeFreeList(ctx, block->next);

			const MemoryBlock *next = block->next;
			
			block->size += next->size + sizeof(MemoryBlock);
			block->next = next->next;

			if (block->next != nullptr) {
				block->next->prev = block;
			} else {
				ctx->lastBlock = block;
			}

			ctx->freeSpace += sizeof(MemoryBlock);
		}

		block->freeNext = nullptr;
		block->freePrev = nullptr;

		insertFreeList(ctx, block);
	}

	void VirtualAllocator::growHeap(AllocContext *ctx, const u64 minSize, const bool isUserAlloc) {
		const usize totalSize = minSize + sizeof(MemoryBlock);
		const auto allocSize = alignUp<usize>(totalSize, pageSize);

		auto *baseAddress = reinterpret_cast<u64 *>(reinterpret_cast<u64>(ctx->heapStart) + ctx->heapSize);

		for (usize offset = 0; offset < allocSize; offset += pageSize) {
			const u64 *newPage = CommonMain::getInstance()->getPMM()->allocPages(1, false);

			if (newPage == nullptr) {
				CommonMain::getTerminal()->error("Could not allocate a new page!", "VirtualAllocator");

				return;
			}

			ctx->pageMap.mapPage(reinterpret_cast<u64>(baseAddress) + offset, reinterpret_cast<u64>(newPage), ctx->pageFlags | ((isUserAlloc & 0b1) << 2), ctx->pageMap.getIsKernel(), false);

			memset(reinterpret_cast<u64 *>(reinterpret_cast<u64>(baseAddress) + offset), 0, pageSize);
		}

		auto* newBlock = reinterpret_cast<MemoryBlock *>(baseAddress);
		newBlock->size = allocSize - sizeof(MemoryBlock);
		newBlock->free = true;
		newBlock->next = nullptr;
		newBlock->freeNext = nullptr;
		newBlock->freePrev = nullptr;

		MemoryBlock* last = ctx->lastBlock;

		if (last != nullptr) {
			newBlock->prev = last;
			last->next     = newBlock;
			ctx->lastBlock = newBlock;
		} else {
			newBlock->prev = nullptr;
			ctx->blocks    = newBlock;
			ctx->lastBlock = newBlock;
		}

		ctx->heapSize += allocSize;
		ctx->freeSpace += newBlock->size;

		defrag(ctx, newBlock);
	}

	void VirtualAllocator::shrinkHeap(AllocContext *ctx) {
		MemoryBlock* current = ctx->lastBlock;

		if (current == nullptr) {
			return;
		}

		MemoryBlock* prev = current->prev;

		if (not current->free or current->size < pageSize) {
			return;
		}

		const u64 blockStart = reinterpret_cast<u64>(current);

		const usize totalSize = sizeof(MemoryBlock) + current->size;
		const usize pagesToFree = totalSize / pageSize;

		if (pagesToFree * pageSize != totalSize) {
			return;
		}

		removeFreeList(ctx, current);

		for (usize i = 0; i < pagesToFree * pageSize; i += pageSize) {
			auto *virtAddress = reinterpret_cast<u64 *>(blockStart + i);

			ctx->pageMap.unMapPage(reinterpret_cast<u64>(virtAddress));

			CommonMain::getInstance()->getPMM()->freePagesCtx(ctx, virtAddress, 1);
		}

		if (prev != nullptr) {
			prev->next = nullptr;
			ctx->lastBlock = prev;
		} else {
			ctx->blocks = nullptr;
			ctx->lastBlock = nullptr;
		}

		ctx->heapSize -= pagesToFree * pageSize;
		ctx->freeSpace -= pagesToFree * pageSize;
	}

	usize VirtualAllocator::getSizeClassIndex(const usize size) {
		for (usize i = 0; i < SIZE_CLASS_COUNT; i++) {
			if (size <= sizeClasses[i]) {
				return i;
			}
		}

		return SIZE_CLASS_COUNT - 1;
	}

	void VirtualAllocator::insertFreeList(AllocContext *ctx, MemoryBlock *block) {
		const usize idx = getSizeClassIndex(block->size);

		block->freeNext = ctx->freeLists[idx];
		block->freePrev = nullptr;

		if (ctx->freeLists[idx] != nullptr) {
			ctx->freeLists[idx]->freePrev = block;
		}

		ctx->freeLists[idx] = block;
	}

	void VirtualAllocator::removeFreeList(AllocContext *ctx, MemoryBlock *block) {
		const usize idx = getSizeClassIndex(block->size);

		if (block->freePrev != nullptr) {
			block->freePrev->freeNext = block->freeNext;
		} else {
			// block is the head of its class list
			ctx->freeLists[idx] = block->freeNext;
		}

		if (block->freeNext != nullptr) {
			block->freeNext->freePrev = block->freePrev;
		}

		block->freeNext = nullptr;
		block->freePrev = nullptr;
	}

	bool VirtualAllocator::areAdjacent(const MemoryBlock *left, const MemoryBlock *right) {
		if (left == nullptr || right == nullptr) {
			return false;
		}

		const u64 expectedRight = reinterpret_cast<u64>(left) + sizeof(MemoryBlock) + left->size;

		return expectedRight == reinterpret_cast<u64>(right);
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
}
