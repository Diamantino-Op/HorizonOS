#include "PhysicalMemory.hpp"

#include "CommonMain.hpp"
#include "MainMemory.hpp"
#include "Math.hpp"
#include "utils/LimineHelper.hpp"

#include "limine.h"

extern limine_memmap_request memMapRequest;

namespace kernel::common::memory {
	namespace {
		bool validRange(const u64 base, const u64 pages) {
			if (pages == 0 || pages > (~0ULL / pageSize)) {
				return false;
			}

			const u64 size = pages * pageSize;

			return base <= ~0ULL - size;
		}

	}

	void PhysicalMemoryManager::removeFreeEntry(PmmListEntry *entry) {
		if (entry == nullptr) {
			return;
		}

		if (entry->prev != nullptr) {
			entry->prev->next = entry->next;
		} else {
			this->listPtr = entry->next;
		}

		if (entry->next != nullptr) {
			entry->next->prev = entry->prev;
		}

		entry->prev = nullptr;
		entry->next = nullptr;
		entry->count = 0;
	}

	bool PhysicalMemoryManager::addFreeRangePhys(u64 phys, usize pageAmount, const bool clearPages, const bool addToTotal, const char *source) {
		if (phys == 0) {
			phys += pageSize;

			if (pageAmount == 0) {
				return false;
			}

			pageAmount--;
		}

		if ((phys & (pageSize - 1)) != 0 || !validRange(phys, pageAmount)) {
			return false;
		}

		const u64 hhdm = CommonMain::getCurrentHhdm();
		const u64 size = pageAmount * pageSize;
		const u64 end = phys + size;

		PmmListEntry *prev = nullptr;
		PmmListEntry *next = this->listPtr;

		while (next != nullptr) {
			const u64 nextPhys = reinterpret_cast<u64>(next) - hhdm;

			if (nextPhys >= phys) {
				break;
			}

			prev = next;
			next = next->next;
		}

		if (prev != nullptr) {
			const u64 prevPhys = reinterpret_cast<u64>(prev) - hhdm;
			const u64 prevEnd = prevPhys + prev->count * pageSize;

			if (prevEnd > phys) {
				CommonMain::getTerminal()->error("Rejected overlapping %s free phys=0x%.16lx pages=%lu existing=0x%.16lx existingPages=%lu",
					"PMM", source, phys, pageAmount, prevPhys, prev->count);

				return false;
			}
		}

		if (next != nullptr) {
			const u64 nextPhys = reinterpret_cast<u64>(next) - hhdm;

			if (end > nextPhys) {
				CommonMain::getTerminal()->error("Rejected overlapping %s free phys=0x%.16lx pages=%lu existing=0x%.16lx existingPages=%lu",
					"PMM", source, phys, pageAmount, nextPhys, next->count);

				return false;
			}
		}

		if (clearPages) {
			memset(reinterpret_cast<void *>(phys + hhdm), 0, size);
		}

		PmmListEntry *entry = nullptr;

		if (prev != nullptr) {
			const u64 prevPhys = reinterpret_cast<u64>(prev) - hhdm;
			const u64 prevEnd = prevPhys + prev->count * pageSize;

			if (prevEnd == phys) {
				prev->count += pageAmount;
				entry = prev;
			}
		}

		if (entry == nullptr) {
			entry = reinterpret_cast<PmmListEntry *>(phys + hhdm);
			entry->prev = prev;
			entry->count = pageAmount;
			entry->next = next;

			if (prev != nullptr) {
				prev->next = entry;
			} else {
				this->listPtr = entry;
			}

			if (next != nullptr) {
				next->prev = entry;
			}
		}

		if (next != nullptr) {
			const u64 entryPhys = reinterpret_cast<u64>(entry) - hhdm;
			const u64 entryEnd = entryPhys + entry->count * pageSize;
			const u64 nextPhys = reinterpret_cast<u64>(next) - hhdm;

			if (entryEnd == nextPhys) {
				entry->count += next->count;
				this->removeFreeEntry(next);
			}
		}

		if (addToTotal) {
			this->totalMemory += size;
		}

		return true;
	}

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

					terminal->debug("Found Usable entry: 0x%.16lx, limine: 0x%.16lx", "PMM", base + CommonMain::getCurrentHhdm(), entry);
					this->addFreeRangePhys(base, (end - base) / pageSize, false, true, "usable");
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

				this->addFreeRangePhys(base, (end - base) / pageSize, false, true, "reclaimable");
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

		PmmListEntry *currEntry = this->listPtr;

		while (currEntry != nullptr) {
			if (currEntry->count >= pageAmount) {
				const u64 currPhys = reinterpret_cast<u64>(currEntry) - CommonMain::getCurrentHhdm();
				const usize remainingPages = currEntry->count - pageAmount;
				const u64 retPhys = currPhys + (remainingPages * pageSize);
				const u64 retAddress = retPhys + CommonMain::getCurrentHhdm();

				if (remainingPages == 0) {
					this->removeFreeEntry(currEntry);
				} else {
					currEntry->count = remainingPages;
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

		if (phys == 0 || pageAmount == 0 || (phys & (pageSize - 1)) != 0 || pageAmount > (~static_cast<usize>(0) / pageSize)) {
			return;
		}

		const bool prevIF = this->pmmSpinLock.lock();
		this->addFreeRangePhys(phys, pageAmount, true, false, "runtime");
		this->pmmSpinLock.unlock(prevIF);
	}

	u64 PhysicalMemoryManager::getFreeMemory() const {
		auto *self = const_cast<PhysicalMemoryManager *>(this);
		const bool prevIF = self->pmmSpinLock.lock();
		const PmmListEntry *currEntry = this->listPtr;

		u64 totFreeMemory = 0;

		while (currEntry != nullptr) {
			totFreeMemory += currEntry->count * pageSize;

			currEntry = currEntry->next;
		}

		self->pmmSpinLock.unlock(prevIF);

		return totFreeMemory;
	}

	u64 PhysicalMemoryManager::getTotalMemory() const {
		return this->totalMemory;
	}
} // namespace kernel::common::memory
