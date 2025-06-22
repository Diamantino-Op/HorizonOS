#include "VirtualAllocator.hpp"

#include "CommonMain.hpp"
#include "Math.hpp"
#include "PhysicalMemory.hpp"
#include "MainMemory.hpp"

extern limine_memmap_request memMapRequest;

namespace kernel::common::memory {
	// TODO: Change page flags to a class for multi arch
	AllocContext *VirtualAllocator::createContext(const bool isUserspace, const bool isProcess) {
		AllocContext *ctx = nullptr;

		if (isProcess) {
			ctx = new AllocContext();

#ifdef HORIZON_USE_NEW_ALLOCATOR
			ctx->libAlloc = new LibAlloc(ctx);
#endif
		} else {
			const u64 ctxPage = reinterpret_cast<u64>(CommonMain::getInstance()->getPMM()->allocPages(1, true));

#ifdef HORIZON_USE_NEW_ALLOCATOR
			memset(reinterpret_cast<u64 *>(ctxPage), 0, sizeof(AllocContext) + sizeof(LibAlloc));
#endif

			ctx = reinterpret_cast<AllocContext *>(ctxPage);

#ifdef HORIZON_USE_NEW_ALLOCATOR
			ctx->libAlloc = reinterpret_cast<LibAlloc *>(ctxPage + sizeof(AllocContext));

			ctx->libAlloc->setCtx(ctx);
#endif
		}

		ctx->isUserspace = isUserspace;

		ctx->pageMap = PageMap();

#ifdef HORIZON_USE_NEW_ALLOCATOR
		ctx->heapSize = 0;
#else
		ctx->heapSize = pageSize;
#endif

		ctx->pageFlags = 0b00000011;

		if (isProcess) {
			if (isUserspace) {
				ctx->pageFlags |= 0b00000100;
			}

			ctx->heapStart = reinterpret_cast<u64 *>(pageSize);
		} else {
			ctx->heapStart = reinterpret_cast<u64 *>(alignUp<u64>(reinterpret_cast<u64>(&dataEnd), pageSize)) + pageSize;
		}

#ifndef HORIZON_USE_NEW_ALLOCATOR
		ctx->blocks = reinterpret_cast<MemoryBlock *>(ctx->heapStart);
#endif

		ctx->pageMap.init(CommonMain::getInstance()->getPMM()->allocPages(1, true));

		memset(ctx->pageMap.getPageTable(), 0, pageSize);

#ifndef HORIZON_USE_NEW_ALLOCATOR
		for (u64 i = reinterpret_cast<u64>(ctx->heapStart); i < reinterpret_cast<u64>(ctx->heapStart) + ctx->heapSize; i += pageSize) {
			u64 *newPage = CommonMain::getInstance()->getPMM()->allocPages(1, false);

			if (not newPage) {
				CommonMain::getTerminal()->error("Could not allocate a new page!", "VirtualAllocator");
			}

			ctx->pageMap.mapPage(i, reinterpret_cast<u64>(newPage), ctx->pageFlags, true, false);
		}
#endif

		return ctx;
	}

#ifndef HORIZON_USE_NEW_ALLOCATOR
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
#endif

	// TODO: Maybe set to 0 too
	u64 *VirtualAllocator::alloc(AllocContext *ctx, const u64 size) {
#ifdef HORIZON_USE_NEW_ALLOCATOR
		u64 *retAddr = ctx->libAlloc->malloc(size);

		if (retAddr == nullptr) {
			CommonMain::getTerminal()->error("Alloc nullptr for size %lu!", "VirtualAllocator", size);
		}

		return retAddr;
#else
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
#endif
	}

	// TODO: Maybe improve speed by defragging only the current block
	void VirtualAllocator::free(AllocContext *ctx, u64 *ptr) {
#ifdef HORIZON_USE_NEW_ALLOCATOR
		ctx->libAlloc->free(ptr);
#else
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
#endif
	}

#ifndef HORIZON_USE_NEW_ALLOCATOR
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
#endif

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

#ifdef HORIZON_USE_NEW_ALLOCATOR
	LibAlloc::LibAlloc(AllocContext *ctx) : ctx(ctx) {}

	void LibAlloc::setCtx(AllocContext *ctx) {
		this->ctx = ctx;
	}

	u64 *LibAlloc::alloc(const usize reqSize) const {
		const u64 oldHeapSize = ctx->heapSize;
		ctx->heapSize += pageSize * reqSize;

		if (reinterpret_cast<u64>(ctx->heapStart) + oldHeapSize > 0xFFFFFFFFFFFFFFFFull - (pageSize * reqSize)) {
			return nullptr;
		}

		if (CommonMain::getInstance()->getPMM()->getFreeMemory() < pageSize * reqSize) {
			return nullptr;
		}

		for (u64 i = reinterpret_cast<u64>(ctx->heapStart) + oldHeapSize; i < reinterpret_cast<u64>(ctx->heapStart) + ctx->heapSize; i += pageSize) {
			u64 *newPage = CommonMain::getInstance()->getPMM()->allocPages(1, false);

			if (not newPage) {
				CommonMain::getTerminal()->error("Could not allocate a new page!", "LibAlloc");
			}

			ctx->pageMap.mapPage(i, reinterpret_cast<u64>(newPage), ctx->pageFlags, true, false);
		}

		return reinterpret_cast<u64 *>(reinterpret_cast<u64>(ctx->heapStart) + oldHeapSize);
	}

	u32 LibAlloc::free(u64 *ptr, const usize reqSize) const {
		for (u64 i = reinterpret_cast<u64>(ptr); i < reinterpret_cast<u64>(ptr) + (pageSize * reqSize); i += pageSize) {
			memset(reinterpret_cast<u64 *>(i), 0, pageSize);
		}

		CommonMain::getInstance()->getPMM()->freePages(ptr, reqSize);

		for (u64 i = reinterpret_cast<u64>(ptr); i < reinterpret_cast<u64>(ptr) + (pageSize * reqSize); i += pageSize) {
			ctx->pageMap.unMapPage(i);
		}

		return 0;
	}

	u64 *LibAlloc::malloc(usize reqSize) {
		u32 startedBet = 0;
		u64 bestSize = 0;
		u64 *p = nullptr;

		u64 size = reqSize;

		// For alignment, we adjust size so there's enough space to align.
		if constexpr (ALIGNMENT > 1) {
			size += ALIGNMENT + ALIGN_INFO;
		}

		/*const u64 prevSize = size;

		size = alignUp<u64>(size, ALIGNMENT);

		reqSize += size - prevSize;*/

		this->lock.lock();

		if (size == 0) {
			this->warningCount += 1;

			CommonMain::getTerminal()->error("alloc(0) called from 0x%.16lx", "LibAlloc", __builtin_return_address(0));

			this->lock.unlock();

			return this->malloc(1);
		}

		if (this->memRoot == nullptr) {
			// This is the first time we are being used.
			this->memRoot = this->allocateNewPage(size);

			if (this->memRoot == nullptr) {
				this->lock.unlock();

				CommonMain::getTerminal()->error("MemRoot null!", "LibAlloc");

				return nullptr;
			}
		}

		// Now we need to bounce through every major and find enough space...
		LibAllocMajor *maj = this->memRoot;

		// Start at the best bet...
		if (this->bestBet != nullptr) {
			bestSize = this->bestBet->size - this->bestBet->usage;

			if (bestSize > (size + sizeof(LibAllocMinor))) {
				maj = this->bestBet;

				startedBet = 1;
			}
		}

		while (maj != nullptr) {
			uPtr diff = maj->size - maj->usage; // Free memory in the block

			if (bestSize < diff) {
				// Hmm... this one has more memory than our bestBet. Remember!
				this->bestBet = maj;
				bestSize = diff;
			}

			// CASE 1: There is not enough space in this major block.
			if (diff < (size + sizeof(LibAllocMinor))) {
				// Another major block next to this one?
				if (maj->next != nullptr) {
					// Hop to that one.
					maj = maj->next;

					continue;
				}

				// If we started at the best bet, let's start all over again.
				if (startedBet == 1) {
					maj = this->memRoot;
					startedBet = 0;

					continue;
				}

				// Create a new major block next to this one and...
				maj->next = this->allocateNewPage(size); // The next one will be okay.

				// no more memory.
				if (maj->next == nullptr) {
					CommonMain::getTerminal()->error("No more mem for case 1!", "LibAlloc");

					break;
				}

				maj->next->prev = maj;
				maj = maj->next;

				// ... fall through to CASE 2 ...
			}

			// CASE 2: It's a brand-new block.
			if (maj->first == nullptr) {
				maj->first = reinterpret_cast<LibAllocMinor *>(reinterpret_cast<uPtr>(maj) + sizeof(LibAllocMajor));

				maj->first->magic = LIBALLOC_MAGIC;
				maj->first->prev = nullptr;
				maj->first->next = nullptr;
				maj->first->block = maj;
				maj->first->size = size;
				maj->first->reqSize = reqSize;
				maj->usage += size + sizeof(LibAllocMinor);

				this->inUse += size;

				p = reinterpret_cast<u64 *>(reinterpret_cast<uPtr>(maj->first) + sizeof(LibAllocMinor));

				p = align(p);

				this->lock.unlock();

				return p;
			}

			// CASE 3: Block in use and enough space at the start of the block.
			diff = reinterpret_cast<uPtr>(maj->first);
			diff -= reinterpret_cast<uPtr>(maj);
			diff -= sizeof(LibAllocMajor);

			if (diff >= (size + sizeof(LibAllocMinor))) {
				// Yes, space in front. Squeeze in.
				maj->first->prev = reinterpret_cast<LibAllocMinor *>(reinterpret_cast<uPtr>(maj) + sizeof(LibAllocMajor));
				maj->first->prev->next = maj->first;
				maj->first = maj->first->prev;

				maj->first->magic = LIBALLOC_MAGIC;
				maj->first->prev = nullptr;
				maj->first->block = maj;
				maj->first->size = size;
				maj->first->reqSize = reqSize;
				maj->usage += size + sizeof(LibAllocMinor);

				this->inUse += size;

				p = reinterpret_cast<u64 *>(reinterpret_cast<uPtr>(maj->first) + sizeof(LibAllocMinor));

				p = align(p);

				this->lock.unlock();

				return p;
			}

			// CASE 4: There is enough space in this block. But is it contiguous?
			LibAllocMinor *min = maj->first;

			// Looping within the block now...
			while (min != nullptr) {
				// CASE 4.1: End of minors in a block. Space from last and end?
				if (min->next == nullptr) {
					// The rest of this block is free... is it big enough?
					diff = reinterpret_cast<uPtr>(maj) + maj->size;
					diff -= reinterpret_cast<uPtr>(min);
					diff -= sizeof(LibAllocMinor);
					diff -= min->size;

					// minus already existing usage...
					if (diff >= (size + sizeof(LibAllocMinor))) {
						// yay...
						min->next = reinterpret_cast<LibAllocMinor *>(reinterpret_cast<uPtr>(min) + sizeof(LibAllocMinor) + min->size);
						min->next->prev = min;
						min = min->next;
						min->next = nullptr;
						min->magic = LIBALLOC_MAGIC;
						min->block = maj;
						min->size = size;
						min->reqSize = reqSize;
						maj->usage += size + sizeof(LibAllocMinor);

						this->inUse += size;

						p = reinterpret_cast<u64 *>(reinterpret_cast<uPtr>(min) + sizeof(LibAllocMinor));

						p = align(p);

						this->lock.unlock();

						return p;
					}
				}

				// CASE 4.2: Is there space between two minors?
				if (min->next != nullptr) {
					// Is the difference between here and next big enough?
					diff = reinterpret_cast<uPtr>(min->next);
					diff -= reinterpret_cast<uPtr>(min);
					diff -= sizeof(LibAllocMinor);
					diff -= min->size;

					if (diff >= (size + sizeof(LibAllocMinor))) {
						auto *newMin = reinterpret_cast<LibAllocMinor *>(reinterpret_cast<uPtr>(min) + sizeof(LibAllocMinor) + min->size);

						newMin->magic = LIBALLOC_MAGIC;
						newMin->next = min->next;
						newMin->prev = min;
						newMin->size = size;
						newMin->reqSize = reqSize;
						newMin->block = maj;

						min->next->prev = newMin;
						min->next = newMin;

						maj->usage += size + sizeof(LibAllocMinor);

						this->inUse += size;

						p = reinterpret_cast<u64 *>(reinterpret_cast<uPtr>(newMin) + sizeof(LibAllocMinor));

						p = align(p);

						this->lock.unlock();

						return p;
					}
				}

				min = min->next;
			}

			// CASE 5: Block full! Ensure the next block and loop.
			if (maj->next == nullptr) {
				if (startedBet == 1) {
					maj = this->memRoot;
					startedBet = 0;

					continue;
				}

				// We've run out. We need more...
				maj->next = this->allocateNewPage(size); // Next one guaranteed to be okay

				// Uh oh, no more memory...
				if (maj->next == nullptr) {
					CommonMain::getTerminal()->error("No more mem for case 5!", "LibAlloc");

					break;
				}

				maj->next->prev = maj;
			}

			maj = maj->next;
		}

		this->lock.unlock();

		CommonMain::getTerminal()->error("Out of memory! Requested: %lu, To alloc: %lu", "LibAlloc", reqSize, size);

		return nullptr;
	}

	u64 *LibAlloc::realloc(u64 *ptr, const usize reqSize) {
		// Honor the case of size == 0 => free old and return nullptr
		if (reqSize == 0) {
			free(ptr);

			return nullptr;
		}

		// In the case of a nullptr pointer, return a simple malloc.
		if (ptr == nullptr) {
			return this->malloc(reqSize);
		}

		// Unalign the pointer if required.
		u64 *p = unalign(ptr);

		this->lock.lock();

		auto *min = reinterpret_cast<LibAllocMinor *>(reinterpret_cast<uPtr>(p) - sizeof(LibAllocMinor));

		// Ensure it is a valid structure.
		if (min->magic != LIBALLOC_MAGIC) {
			this->errorCount += 1;

			// Check for overrun errors. For all bytes of LIBALLOC_MAGIC
			if (((min->magic & 0xFFFFFF) == (LIBALLOC_MAGIC & 0xFFFFFF)) or ((min->magic & 0xFFFF) == (LIBALLOC_MAGIC & 0xFFFF)) or ((min->magic & 0xFF) == (LIBALLOC_MAGIC & 0xFF)) ) {
				this->possibleOverruns += 1;

				CommonMain::getTerminal()->error("Possible 1-3 byte overrun for magic 0x%.8lx != 0x%.8lx", "LibAlloc", min->magic, LIBALLOC_MAGIC);
			}

			if (min->magic == LIBALLOC_DEAD) {
				CommonMain::getTerminal()->error("Multiple free(0x%.16lx) called from 0x%.16lx", "LibAlloc", p, __builtin_return_address(0));
			} else {
				CommonMain::getTerminal()->error("Bad free(0x%.16lx) called from 0x%.16lx", "LibAlloc", p, __builtin_return_address(0));
			}

			// being lied to...
			this->lock.unlock();

			return nullptr;
		}

		// Definitely a memory block.

		const u32 realSize = min->reqSize;

		if (realSize >= reqSize) {
			min->reqSize = reqSize;

			this->lock.unlock();

			return ptr;
		}

		this->lock.unlock();

		// If we got here, then we're reallocating to a block bigger than us.

		p = this->malloc(reqSize); // We need to allocate new memory

		memcpy(p, ptr, realSize);

		this->free(ptr);

		return p;
	}

	u64 *LibAlloc::calloc(const usize count, const usize reqSize) {
		const i32 realSize = count * reqSize;

		u64 *p = this->malloc(realSize);

		memset(p, 0, realSize);

		return p;
	}

	void LibAlloc::free(u64 *ptr) {
		if (ptr == nullptr) {
			this->warningCount += 1;

			//CommonMain::getTerminal()->error("free(nullptr) called from 0x%.16lx", "LibAlloc", __builtin_return_address(0));

			return;
		}

		ptr = unalign(ptr);

		this->lock.lock();

		auto *min = reinterpret_cast<LibAllocMinor *>(reinterpret_cast<uPtr>(ptr) - sizeof(LibAllocMinor));

		if (min->magic != LIBALLOC_MAGIC) {
			this->errorCount += 1;

			// Check for overrun errors. For all bytes of LIBALLOC_MAGIC
			if (((min->magic & 0xFFFFFF) == (LIBALLOC_MAGIC & 0xFFFFFF)) or ((min->magic & 0xFFFF) == (LIBALLOC_MAGIC & 0xFFFF)) or ((min->magic & 0xFF) == (LIBALLOC_MAGIC & 0xFF))) {
				this->possibleOverruns += 1;

				CommonMain::getTerminal()->error("Possible 1-3 byte overrun for magic 0x%.8lx != 0x%.8lx", "LibAlloc", min->magic, LIBALLOC_MAGIC);
			}

			if (min->magic == LIBALLOC_DEAD) {
				CommonMain::getTerminal()->error("Multiple free attempts on 0x%.16lx from 0x%.16lx", "LibAlloc", ptr, __builtin_return_address(0));
			} else {
				CommonMain::getTerminal()->error("Bad free(0x%.16lx) called from 0x%.16lx", "LibAlloc", ptr, __builtin_return_address(0));
			}

			// being lied to...

			this->lock.unlock();

			return;
		}

		LibAllocMajor *maj = min->block;

		this->inUse -= min->size;

		maj->usage -= min->size + sizeof(LibAllocMinor);

		min->magic = LIBALLOC_DEAD;

		if (min->next != nullptr) {
			min->next->prev = min->prev;
		}

		if (min->prev != nullptr) {
			min->prev->next = min->next;
		}

		if (min->prev == nullptr) {
			// Might empty the block. This was the first minor.
			maj->first = min->next;
		}

		// We need to clean up after the majors now...

		// Block completely unused.
		if (maj->first == nullptr) {
			if (this->memRoot == maj) {
				this->memRoot = maj->next;
			}

			if (this->bestBet == maj) {
				this->bestBet = nullptr;
			}

			if (maj->prev != nullptr) {
				maj->prev->next = maj->next;
			}

			if (maj->next != nullptr) {
				maj->next->prev = maj->prev;
			}

			this->allocated -= maj->size;

			this->free(reinterpret_cast<u64 *>(maj), maj->pages);
		} else {
			if (this->bestBet != nullptr) {
				const i32 bestSize = this->bestBet->size - this->bestBet->usage;
				const i32 majSize = maj->size - maj->usage;

				if (majSize > bestSize) {
					this->bestBet = maj;
				}
			}
		}

		this->lock.unlock();
	}

	LibAllocMajor *LibAlloc::allocateNewPage(const u32 size) {
		// This is how much space is required.
		u32 st = size + sizeof(LibAllocMajor);
		st += sizeof(LibAllocMinor);

		// Perfect amount of space?
		if ((st % pageSize) == 0) {
			st = st / pageSize;
		} else {
			st = st / pageSize + 1; // No, add the buffer.
		}

		// Make sure it's >= the minimum size.
		if (st < this->pageCount) {
			st = this->pageCount;
		}

		auto *maj = reinterpret_cast<LibAllocMajor *>(this->alloc(st));

		if (maj == nullptr) {
			this->warningCount += 1;

			CommonMain::getTerminal()->error("alloc(%lu) returned nullptr", "LibAlloc", st);

			return nullptr;
		}

		maj->prev = nullptr;
		maj->next = nullptr;
		maj->pages = st;
		maj->size = st * pageSize;
		maj->usage = sizeof(LibAllocMajor);
		maj->first = nullptr;

		this->allocated += maj->size;

		return maj;
	}

	void LibAlloc::dump() const {
		Terminal *terminal = CommonMain::getTerminal();

		LibAllocMajor *maj = this->memRoot;
		LibAllocMinor *min = nullptr;

		terminal->debug("------ Memory data ---------------", "LibAlloc");
		terminal->debug("System memory allocated: %lu bytes", "LibAlloc", this->allocated);
		terminal->debug("Memory in use (allocated): %lu bytes", "LibAlloc", this->inUse);
		terminal->debug("Warning count: %lu", "LibAlloc", this->warningCount);
		terminal->debug("Error count: %lu", "LibAlloc", this->errorCount);
		terminal->debug("Possible overruns: %lu", "LibAlloc", this->possibleOverruns);

		while (maj != nullptr) {
			terminal->debug("0x%.16lx: Total = %lu, Used = %lu", "LibAlloc", maj, maj->size, maj->usage);

			min = maj->first;

			while (min != nullptr) {
				terminal->debug("	0x%.16lx: %lu bytes", "LibAlloc", min, min->size);

				min = min->next;
			}

			maj = maj->next;
		}
	}

	u64 *LibAlloc::align(u64 *ptr) {
		if constexpr (ALIGNMENT > 1) {
			ptr = reinterpret_cast<u64 *>(reinterpret_cast<uPtr>(ptr) + ALIGN_INFO);
			uPtr diff = reinterpret_cast<uPtr>(ptr) & (ALIGNMENT - 1);

			if (diff != 0) {
				diff = ALIGNMENT - diff;
				ptr = reinterpret_cast<u64 *>(reinterpret_cast<uPtr>(ptr) + diff);
			}

			*reinterpret_cast<ALIGN_TYPE *>(reinterpret_cast<uPtr>(ptr) - ALIGN_INFO) = diff + ALIGN_INFO;
		}

		return ptr;
	}

	u64 *LibAlloc::unalign(u64 *ptr) {
		if constexpr (ALIGNMENT > 1) {
			if (const uPtr diff = *reinterpret_cast<ALIGN_TYPE *>(reinterpret_cast<uPtr>(ptr) - ALIGN_INFO); diff < (ALIGNMENT + ALIGN_INFO)) {
				ptr = reinterpret_cast<u64 *>(reinterpret_cast<uPtr>(ptr) - diff);
			}
		}

		return ptr;
	}
#endif
}