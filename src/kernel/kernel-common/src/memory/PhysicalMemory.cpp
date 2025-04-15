#include "PhysicalMemory.hpp"

#include "CommonMain.hpp"

#include "limine.h"

extern limine_memmap_request memMapRequest;

namespace kernel::common::memory {
	void PhysicalMemoryManager::init() {
		Terminal* terminal = CommonMain::getTerminal();

		u64 usableAmount = 0;

		if (memMapRequest.response != nullptr) {
			for (u64 i = 0; i < memMapRequest.response->entry_count; i++) {
				limine_memmap_entry *entry = memMapRequest.response->entries[i];

				if (entry->type == LIMINE_MEMMAP_USABLE) {
					usableAmount += entry->length;
				}
			}

			u64 reqSize = (usableAmount / pageSize) * sizeof(PmmListEntry);

			for (u64 i = 0; i < memMapRequest.response->entry_count; i++) {
				limine_memmap_entry *entry = memMapRequest.response->entries[i];

				if (entry->type == LIMINE_MEMMAP_USABLE && entry->length >= reqSize) {
					this->listPtr = reinterpret_cast<uPtr *>(entry->base);
					this->listSize = reqSize;

					terminal->printf("PMM Table located at: 0x%.16lx, With size: %llu\n", this->listPtr,  this->listSize);

					entry->base += reqSize;
					entry->length -= reqSize;

					break;
				}
			}

			u64 currEntry = 0;

			for (u64 i = 0; i < memMapRequest.response->entry_count; i++) {
				limine_memmap_entry *entry = memMapRequest.response->entries[i];

				if (entry->type == LIMINE_MEMMAP_USABLE) {
					u64 tmpJ = 0;

					for (u64 j = 0; j < entry->length / pageSize; j++) {
						reinterpret_cast<PmmListEntry*>(this->listPtr + (pageSize * (currEntry + j)))->address = entry->base + (j * pageSize);

						tmpJ = j;
					}

					currEntry += tmpJ;
				}
			}
		}
	}
}