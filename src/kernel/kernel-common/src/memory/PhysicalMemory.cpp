#include "PhysicalMemory.hpp"

#include "CommonMain.hpp"
#include "MainMemory.hpp"
#include "Math.hpp"
#include "utils/LimineHelper.hpp"

#include "limine.h"

extern limine_memmap_request memMapRequest;

namespace kernel::common::memory {
	void PhysicalMemoryManager::init() {
		Terminal* terminal = CommonMain::getTerminal();

		terminal->info("Initializing Physical Memory Manager...", "PMM");

		if (memMapRequest.response != nullptr) {
			for (u64 i = 0; i < memMapRequest.response->entry_count; i++) {
				if (const limine_memmap_entry *entry = memMapRequest.response->entries[i]; entry->type == LIMINE_MEMMAP_USABLE) {
					if (entry->length == 0 || entry->base > ~0ULL - entry->length) {
						continue;
					}

					const u64 base = alignUp<u64>(entry->base, pageSize);
					const u64 end = alignDown<u64>(entry->base + entry->length, pageSize);

					if (end <= base) {
						continue;
					}

					this->totalMemory += end - base;

					auto *currEntry = reinterpret_cast<PmmListEntry *>(base + CommonMain::getCurrentHhdm());

					terminal->debug("Found Usable entry: 0x%.16lx, limine: 0x%.16lx", "PMM", currEntry, entry);

					currEntry->count = (end - base) / pageSize;

					terminal->debug("New Usable entry found: Base: 0x%.16lx, Size: %llu", "PMM", currEntry, currEntry->count * pageSize);

					currEntry->prev = nullptr;
					currEntry->next = this->listPtr;

					if (this->listPtr != nullptr) {
						this->listPtr->prev = currEntry;
					}

					this->listPtr = currEntry;
				}
			}
		}
	}

	void PhysicalMemoryManager::reclaimMemory() {
		const bool prevIF = this->pmmSpinLock.lock();

		if (this->bootloaderMemoryReclaimed) {
			this->pmmSpinLock.unlock(prevIF);

			return;
		}

		this->bootloaderMemoryReclaimed = true;

		if (memMapRequest.response != nullptr) {
			const auto addReclaimableRange = [this](const u64 base, const u64 end) -> void {
				if (end <= base) {
					return;
				}

				auto *currEntry = reinterpret_cast<PmmListEntry *>(base + CommonMain::getCurrentHhdm());

				currEntry->count = (end - base) / pageSize;
				this->totalMemory += currEntry->count * pageSize;

				currEntry->prev = nullptr;
				currEntry->next = this->listPtr;

				if (this->listPtr != nullptr) {
					this->listPtr->prev = currEntry;
				}

				this->listPtr = currEntry;
			};

			for (u64 i = 0; i < memMapRequest.response->entry_count; i++) {
				if (const limine_memmap_entry *entry = memMapRequest.response->entries[i]; entry->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE) {
					if (entry->length == 0 || entry->base > ~0ULL - entry->length) {
						continue;
					}

					const u64 base = alignUp<u64>(entry->base, pageSize);
					const u64 end = alignDown<u64>(entry->base + entry->length, pageSize);

					if (end <= base) {
						continue;
					}

					u64 reclaimableBase = base;

					for (u64 page = base; page < end; page += pageSize) {
						if (utils::doesPhysicalRangeOverlapLimineModuleOrMpResponse(page, pageSize)) {
							addReclaimableRange(reclaimableBase, page);
							reclaimableBase = page + pageSize;
						}
					}

					addReclaimableRange(reclaimableBase, end);
				}
			}
		}

		this->pmmSpinLock.unlock(prevIF);
	}

	u64 *PhysicalMemoryManager::allocPages(const usize pageAmount, const bool useHhdm) {
		if (pageAmount == 0) {
			return nullptr;
		}

		const bool prevIF = this->pmmSpinLock.lock();

		const PmmListEntry *currEntry = this->listPtr;

		while (currEntry != nullptr) {
			if (currEntry->count >= pageAmount) {
				const auto retAddress = reinterpret_cast<u64>(currEntry);

				if (currEntry->count == pageAmount) {
					if (currEntry->prev != nullptr) {
						currEntry->prev->next = currEntry->next;
					} else {
						this->listPtr = currEntry->next;
					}

					if (currEntry->next != nullptr) {
						currEntry->next->prev = currEntry->prev;
					}
				} else {
					auto *newEntry = reinterpret_cast<PmmListEntry *>(reinterpret_cast<u64>(currEntry) + (pageAmount * pageSize));

					memcpy(newEntry, currEntry, sizeof(PmmListEntry));

					newEntry->count -= pageAmount;

					if (newEntry->prev != nullptr) {
						newEntry->prev->next = newEntry;
					} else {
						this->listPtr = newEntry;
					}

					if (newEntry->next != nullptr) {
						newEntry->next->prev = newEntry;
					}
				}

				memset(reinterpret_cast<u64 *>(retAddress), 0, pageAmount * pageSize);

				if (useHhdm) {
					this->pmmSpinLock.unlock(prevIF);

					return reinterpret_cast<u64 *>(retAddress);
				}

				this->pmmSpinLock.unlock(prevIF);

				return reinterpret_cast<u64 *>(retAddress - CommonMain::getCurrentHhdm());
			}

			currEntry = currEntry->next;
		}

		this->pmmSpinLock.unlock(prevIF);

		return nullptr;
	}

	void PhysicalMemoryManager::freePages(u64 *virtAddress, const usize pageAmount) {
		this->freePagesCtx(CommonMain::getInstance()->getKernelAllocContext(), virtAddress, pageAmount);
	}

	void PhysicalMemoryManager::freePagesCtx(const AllocContext *ctx, u64 *virtAddress, const usize pageAmount) {
		const u64 phys = ctx->pageMap.getPhysAddress(reinterpret_cast<u64>(virtAddress));

		if (phys == 0) {
			return;
		}

		this->freePagesPhys(reinterpret_cast<u64 *>(alignDown<u64>(phys, pageSize)), pageAmount);
	}

	void PhysicalMemoryManager::freePagesPhys(const u64 *physAddress, const usize pageAmount) {
		const u64 phys = reinterpret_cast<u64>(physAddress);

		if (phys == 0 || pageAmount == 0 || (phys & (pageSize - 1)) != 0) {
			return;
		}

		const bool prevIF = this->pmmSpinLock.lock();

		auto *currEntry = reinterpret_cast<PmmListEntry *>(phys + CommonMain::getCurrentHhdm());

		memset(currEntry, 0, pageAmount * pageSize);

		currEntry->count = pageAmount;

		currEntry->prev = nullptr;
		currEntry->next = this->listPtr;

		if (this->listPtr != nullptr) {
			this->listPtr->prev = currEntry;
		}

		this->listPtr = currEntry;

		this->pmmSpinLock.unlock(prevIF);
	}

	u64 PhysicalMemoryManager::getFreeMemory() const {
		const PmmListEntry *currEntry = this->listPtr;

		u64 totFreeMemory = 0;

		while (currEntry != nullptr) {
			totFreeMemory += currEntry->count * pageSize;

			currEntry = currEntry->next;
		}

		return totFreeMemory;
	}

	u64 PhysicalMemoryManager::getTotalMemory() const {
		return this->totalMemory;
	}
} // namespace kernel::common::memory
