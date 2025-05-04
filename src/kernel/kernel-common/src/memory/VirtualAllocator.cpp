#include "VirtualAllocator.hpp"

#include "CommonMain.hpp"
#include "Math.hpp"
#include "PhysicalMemory.hpp"
#include "MainMemory.hpp"

namespace kernel::common::memory {
	// TODO: Change page flags to a class for multiarch
	AllocContext *VirtualAllocator::createContext(const bool isUserspace) {
		auto *ctx = reinterpret_cast<AllocContext *>(CommonMain::getInstance()->getPMM()->allocPages(1, true));

		ctx->pageMap = PageMap();
		ctx->heapSize = pageSize;

		ctx->pageFlags = 0b00000011;

		if (isUserspace) {
			ctx->pageFlags |= 0b00000100;

			ctx->heapStart = reinterpret_cast<u64 *>(CommonMain::getCurrentHhdm());
		} else {
			ctx->heapStart = reinterpret_cast<u64 *>(alignUp<u64>(reinterpret_cast<u64>(&dataEnd), pageSize));
		}

		ctx->blocks = reinterpret_cast<MemoryBlock *>(ctx->heapStart);

		ctx->pageMap.init(CommonMain::getInstance()->getPMM()->allocPages(1, true));

		memset(ctx->pageMap.getPageTable(), 0, pageSize);

		ctx->pageMap.mapPage(reinterpret_cast<u64>(ctx->heapStart), reinterpret_cast<u64>(CommonMain::getInstance()->getPMM()->allocPages(1, false)), ctx->pageFlags, false, false);

		return ctx;
	}

	void VirtualAllocator::initContext(const AllocContext *ctx) {
		memset(ctx->heapStart, 0, pageSize);

		ctx->blocks->size = ctx->heapSize - sizeof(MemoryBlock);
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

		MemoryBlock* current = ctx->blocks;

		while (current) {
			if (current->free && current->size >= size) {
				if (current->size >= size + sizeof(MemoryBlock) + minBlockSize) {
					auto* newBlock = reinterpret_cast<MemoryBlock *>(reinterpret_cast<u64>(current) + sizeof(MemoryBlock) + size);

					newBlock->size = current->size - size - sizeof(MemoryBlock);
					newBlock->free = true;
					newBlock->next = current->next;
					current->next = newBlock;
				}

				current->free = false;
				current->size = size;

				ctx->lock.unlock();

				return reinterpret_cast<u64 *>(reinterpret_cast<u64>(current) + sizeof(MemoryBlock));
			}

			current = current->next;
		}

		if (CommonMain::getInstance()->getPMM()->getFreeMemory() < size + sizeof(MemoryBlock)) {
			ctx->lock.unlock();

			return nullptr;
		}

		growHeap(ctx, size);

		ctx->lock.unlock();

		return alloc(ctx, size);
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

		memset(reinterpret_cast<u64 *>(reinterpret_cast<u64>(block) + sizeof(MemoryBlock)), 0, block->size);

		defrag(ctx);

		if (ctx->heapSize > pageSize) {
			shrinkHeap(ctx);
		}

		ctx->lock.unlock();
	}

	void VirtualAllocator::defrag(const AllocContext *ctx) {
		MemoryBlock* current = ctx->blocks;

		while (current && current->next) {
			if (current->free && current->next->free) {
				current->size += sizeof(MemoryBlock) + current->next->size;
				current->next = current->next->next;
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
				return;
			}

			ctx->pageMap.mapPage(reinterpret_cast<u64>(baseAddress) + offset, reinterpret_cast<u64>(newPage), ctx->pageFlags, false, false);

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

		defrag(ctx);
	}

	void VirtualAllocator::shrinkHeap(AllocContext *ctx) {
		MemoryBlock* current = ctx->blocks;
		MemoryBlock* prev = nullptr;

		while (current && current->next) {
			prev = current;
			current = current->next;
		}

		if (!current || !current->free || current->size < pageSize) {
			return;
		}

		const u64 blockStart = reinterpret_cast<u64>(current);
		const u64 heapEnd = reinterpret_cast<u64>(ctx->heapStart) + ctx->heapSize;

		if (const u64 blockEnd = blockStart + sizeof(MemoryBlock) + current->size; blockEnd != heapEnd) {
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
	}
}