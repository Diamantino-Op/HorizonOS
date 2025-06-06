#include "PhysicalMemory.hpp"

#include "CommonMain.hpp"
#include "MainMemory.hpp"

#include "limine.h"

extern limine_memmap_request memMapRequest;

namespace kernel::common::memory {
	void PhysicalMemoryManager::init() {
		Terminal* terminal = CommonMain::getTerminal();

		terminal->info("Initializing Physical Memory Manager...", "PMM");

		if (memMapRequest.response != nullptr) {
			for (u64 i = 0; i < memMapRequest.response->entry_count; i++) {
				if (const limine_memmap_entry *entry = memMapRequest.response->entries[i]; entry->type == LIMINE_MEMMAP_USABLE) {
					auto *currEntry = reinterpret_cast<PmmListEntry *>(entry->base + CommonMain::getCurrentHhdm());
					currEntry->count = entry->length / pageSize;

					terminal->debug("New Usable entry found: Base: 0x%.16lx, Size: %llu", "PMM", currEntry, currEntry->count * pageSize);

					currEntry->next = this->listPtr;

					if (this->listPtr != nullptr) {
						this->listPtr->prev = currEntry;
					}

					this->listPtr = currEntry;
				}
			}
		}
	}

	u64 *PhysicalMemoryManager::allocPages(const usize pageAmount, const bool useHhdm) {
		this->pmmSpinLock.lock();

		PmmListEntry *currEntry = this->listPtr;

		while (currEntry != nullptr) {
			if (currEntry->count >= pageAmount) {
				const auto retAddress = reinterpret_cast<u64>(currEntry);

				if (currEntry->count == pageAmount) {
					if (currEntry->prev != nullptr) {
						currEntry->prev->next = currEntry->next;
					}

					if (currEntry->next != nullptr) {
						currEntry->next->prev = currEntry->prev;
					}

					if (currEntry->prev) {
						this->listPtr = currEntry->prev;
					} else {
						this->listPtr = currEntry->next;
					}
				} else {
					const auto newAddress = reinterpret_cast<u64 *>(reinterpret_cast<u64>(currEntry) + (pageAmount * pageSize));

					memcpy(newAddress, currEntry, sizeof(PmmListEntry));

					if (this->listPtr == currEntry) {
						this->listPtr = reinterpret_cast<PmmListEntry *>(newAddress);

						this->listPtr->count -= pageAmount;
					} else {
						currEntry = reinterpret_cast<PmmListEntry *>(newAddress);

						currEntry->count -= pageAmount;

						if (currEntry->prev != nullptr) {
							currEntry->prev->next = currEntry;
						}

						if (currEntry->next != nullptr) {
							currEntry->next->prev = currEntry;
						}
					}
				}

				memset(reinterpret_cast<u64 *>(retAddress), 0, pageAmount * pageSize);

				if (useHhdm) {
					this->pmmSpinLock.unlock();

					return reinterpret_cast<u64 *>(retAddress);
				}

				this->pmmSpinLock.unlock();

				return reinterpret_cast<u64 *>(retAddress - CommonMain::getCurrentHhdm());
			}

			currEntry = currEntry->next;
		}

		this->pmmSpinLock.unlock();

		return nullptr;
	}

	void PhysicalMemoryManager::freePages(u64 *virtAddress, const usize pageAmount) {
		this->pmmSpinLock.lock();

		const u64 hhdmVirtAddress = CommonMain::getInstance()->getKernelAllocContext()->pageMap.getPhysAddress(reinterpret_cast<u64>(virtAddress)) + CommonMain::getCurrentHhdm();

		auto *currEntry = reinterpret_cast<PmmListEntry *>(hhdmVirtAddress);

		memset(currEntry, 0, pageAmount * pageSize);

		currEntry->count = pageAmount;

		currEntry->next = this->listPtr;

		if (this->listPtr != nullptr) {
			this->listPtr->prev = currEntry;
		}

		this->listPtr = currEntry;

		this->pmmSpinLock.unlock();
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
}