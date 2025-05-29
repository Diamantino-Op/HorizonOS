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
			ctx->heapStart = reinterpret_cast<u64 *>(alignUp<u64>(reinterpret_cast<u64>(&dataEnd), pageSize));
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

	// TODO: Find a way to not use this! (this is broken when the a middle block corrupts)
	void VirtualAllocator::fixBlock(AllocContext *ctx, MemoryBlock *blockPtr) {
		u64 tmpFree = ctx->freeSpace;

		MemoryBlock *tmp = ctx->blocks;

		while (reinterpret_cast<u64>(tmp) > CommonMain::getCurrentHhdm() and tmp != nullptr) {
			if (tmp->free) {
				tmpFree -= tmp->size;
			}

			tmp = tmp->next;
		}

		const u64 heapEnd = reinterpret_cast<u64>(ctx->heapStart) + ctx->heapSize;
		const u64 blockEnd = reinterpret_cast<u64>(blockPtr) + sizeof(MemoryBlock) + tmpFree;

		if (blockPtr->size > 1'000'000'000'000'000ull) {
			if (blockEnd < heapEnd and reinterpret_cast<u64>(blockPtr->next) < CommonMain::getCurrentHhdm()) {
				blockPtr->next = reinterpret_cast<MemoryBlock *>(blockEnd);
				blockPtr->size = tmpFree;
			} else if (blockEnd > heapEnd) {
				blockPtr->next = nullptr;
				blockPtr->size = tmpFree;
			}
		}
	}

	// TODO: Maybe set to 0 too
	u64 *VirtualAllocator::alloc(AllocContext *ctx, const u64 size) {
		ctx->lock.lock();

		const u64 alignedSize = alignUp<u64>(size, sizeof(MemoryBlock));

		CommonMain::getTerminal()->debug("Allocating %lu bytes", "VirtualAllocator", alignedSize);
		CommonMain::getTerminal()->debug("Free Space: %lu bytes", "VirtualAllocator", ctx->freeSpace);
		CommonMain::getTerminal()->debug("Heap size: %lu bytes", "VirtualAllocator", ctx->heapSize);

		MemoryBlock* current = ctx->blocks;

		while (current != nullptr) {
			if (current->free and current->size >= alignedSize) {
				CommonMain::getTerminal()->debug("AN 1 - Next: 0x%.16lx", "VirtualAllocator", reinterpret_cast<u64>(current->next));

				if (current->size >= alignedSize + sizeof(MemoryBlock) + minBlockSize) {
					CommonMain::getTerminal()->warn("Found block of size: %lu bytes", "VirtualAllocator", current->size);

					if (current->size > pageSize * 1000) {
						CommonMain::getTerminal()->error("AN 1 - Block 0x%.16lx is too big: %lu bytes", "VirtualAllocator", reinterpret_cast<u64>(current), current->size);
					}

					auto* newBlock = reinterpret_cast<MemoryBlock *>(reinterpret_cast<u64>(current) + sizeof(MemoryBlock) + alignedSize);

					newBlock->size = current->size - alignedSize - sizeof(MemoryBlock);
					newBlock->free = true;
					newBlock->next = current->next;
					current->next = newBlock;

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
		ctx->lock.lock();

		if (!ptr) {
			ctx->lock.unlock();

			return;
		}

		auto* block = reinterpret_cast<MemoryBlock *>(reinterpret_cast<u64>(ptr) - sizeof(MemoryBlock));
		block->free = true;

		memset(ptr, 0, block->size);

		ctx->freeSpace += block->size;

		defrag(ctx);

		if (ctx->heapSize > pageSize) {
			shrinkHeap(ctx);
		}

		ctx->lock.unlock();
	}

	void VirtualAllocator::defrag(AllocContext *ctx) {
		MemoryBlock* current = ctx->blocks;

		while (current && current->next) {
			if (current->free && current->next->free) {
				current->size += sizeof(MemoryBlock) + current->next->size;
				current->next = current->next->next;

				if (current->size > pageSize * 1000) {
					CommonMain::getTerminal()->error("DF - Block 0x%.16lx is too big: %lu bytes", "VirtualAllocator", reinterpret_cast<u64>(current), current->size);
				}

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

		if (newBlock->size > pageSize * 1000) {
			CommonMain::getTerminal()->error("GH - Block 0x%.16lx is too big: %lu bytes", "VirtualAllocator", reinterpret_cast<u64>(newBlock), newBlock->size);
		}

		MemoryBlock* last = ctx->blocks;

		while (last->next) {
			last = last->next;
		}

		last->next = newBlock;

		ctx->heapSize += allocSize;
		ctx->freeSpace += newBlock->size;

		defrag(ctx);
	}

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

		if (const u64 blockEnd = blockStart + sizeof(MemoryBlock) + current->size; blockEnd != heapEnd) {
			CommonMain::getTerminal()->error("Heap end is wrong! (0x%.16lx != 0x%.16lx)", "VirtualAllocator", blockEnd, heapEnd);

			return;
		}

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
}