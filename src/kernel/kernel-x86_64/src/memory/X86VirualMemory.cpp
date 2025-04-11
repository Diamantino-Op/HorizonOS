#include "X86VirtualMemory.hpp"

#include "Main.hpp"
#include "memory/MainMemory.hpp"

namespace kernel::common::memory {
	using namespace x86_64;
	using namespace x86_64::memory;

	template<class T> VirtualMemoryManager<T>::VirtualMemoryManager() {
		Terminal* terminal = Kernel::getTerminal();

		this->currentMainPage = PageTable();

		if (hhdmRequest.response != nullptr) {
			this->currentHhdm = hhdmRequest.response->offset;
		}

		terminal->printf("Current HHDM Offset: %l", this->currentHhdm);

		if (pagingModeRequest.response != nullptr) {
			// 0 = Page Level 4, 1 = Page Level 5
			if (pagingModeRequest.response->mode == 0) {
				terminal->printf("Current Paging Mode: Level 4");
			} else if (pagingModeRequest.response->mode == 1) {
				terminal->printf("Current Paging Mode: Level 5");
			}
		}
	}

	template<class T> void VirtualMemoryManager<T>::loadPageTable() {
		Terminal* terminal = Kernel::getTerminal();

		terminal->printf("Loading main page table: %l", reinterpret_cast<uPtr *>(&this->currentMainPage) - this->currentHhdm);

		loadPageTableAsm(reinterpret_cast<uPtr *>(&this->currentMainPage) - this->currentHhdm);
	}

	template<class T> void VirtualMemoryManager<T>::mapPage(u64 vAddr, u64 pAddr, u8 flags) {
		u32 lvl4 = (vAddr >> 39) & 0x1FF;
		u32 lvl3 = (vAddr >> 30) & 0x1FF;
		u32 lvl2 = (vAddr >> 21) & 0x1FF;
		u32 lvl1 = (vAddr >> 12) & 0x1FF;

		uPtr *pdpt = getOrCreatePageTable(&this->currentMainPage, lvl4, flags);
		uPtr *pd = getOrCreatePageTable(pdpt, lvl3, flags);
		PageTable *pt = getOrCreatePageTable(pd, lvl2, flags);

		pt->entries[lvl1].present = 1;

		this->setPageFlags(reinterpret_cast<uPtr *>(&pt->entries[lvl1]), flags);

		pt->entries[lvl1].address = (pAddr >> 12) & 0xFFFFFFFFFF;
	}

	template<class T> void VirtualMemoryManager<T>::unMapPage(u64 vAddr) {
		u32 lvl4 = (vAddr >> 39) & 0x1FF;
		u32 lvl3 = (vAddr >> 30) & 0x1FF;
		u32 lvl2 = (vAddr >> 21) & 0x1FF;
		u32 lvl1 = (vAddr >> 12) & 0x1FF;

		PageTable lvl4Table = this->currentMainPage;

		if (!lvl4Table.entries[lvl4].present) {
			return;
		}

		PageTable *lvl3Table = reinterpret_cast<PageTable *>((lvl4Table.entries[lvl4].address << 12) + this->currentHhdm);
		if (!lvl3Table->entries[lvl3].present) {
			return;
		}

		PageTable *lvl2Table = reinterpret_cast<PageTable *>((lvl3Table->entries[lvl3].address << 12) + this->currentHhdm);
		if (!lvl2Table->entries[lvl2].present) {
			return;
		}

		PageTable *lvl1Table = reinterpret_cast<PageTable *>((lvl2Table->entries[lvl2].address << 12) + this->currentHhdm);
		if (lvl1Table->entries[lvl1].present) {
			memset(&lvl1Table->entries[lvl1], 0, sizeof(lvl1Table->entries[lvl1]));
		}
	}

	template<class T> uPtr* VirtualMemoryManager<T>::getOrCreatePageTable(T* parent, u16 index,u8 flags) {
		PageTable *parentTable = parent;

		if (!parentTable->entries[index].present) {
			PageTable* newTable = nullptr; // alloc_page_table()
			memset(newTable + this->currentHhdm, 0, pageSize);

			this->setPageFlags(reinterpret_cast<uPtr *>(&parentTable->entries[index]), flags);

			parentTable->entries[index].address = (reinterpret_cast<u64>(newTable) >> 12) & 0xFFFFFFFFFF;
		} else {
			this->setPageFlags(reinterpret_cast<uPtr *>(&parentTable->entries[index]), flags);

			parentTable->entries[index].executeDisable = 0;
		}

		return reinterpret_cast<uPtr *>((parentTable->entries[index].address << 12) + this->currentHhdm);
	}

	template<class T> void VirtualMemoryManager<T>::setPageFlags(uPtr *pageAddr, u8 flags) {
		PageEntry *pageEntry = reinterpret_cast<PageEntry *>(pageAddr);

		pageEntry->writeable = (flags >> 1) & 1;
		pageEntry->userAccess = (flags >> 2) & 1;
		pageEntry->writeThrough = (flags >> 3) & 1;
		pageEntry->cacheDisabled = (flags >> 4) & 1;
		pageEntry->accessed = (flags >> 5) & 1;
		pageEntry->dirty = (flags >> 6) & 1;
		pageEntry->size = (flags >> 7) & 1;
	}
}