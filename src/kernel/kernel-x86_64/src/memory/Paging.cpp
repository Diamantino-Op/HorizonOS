#include "Paging.hpp"

namespace kernel::x86_64::memory {
	PagingManager::PagingManager() {

	}

	void PagingManager::loadPageTable() {

	}

	void PagingManager::mapPage(uPtr* level4Page, u64 vAddr, u64 pAddr, u8 flags) {
		u32 lvl4 = (vAddr >> 39) & 0x1FF;
		u32 lvl3 = (vAddr >> 30) & 0x1FF;
		u32 lvl2 = (vAddr >> 21) & 0x1FF;
		u32 lvl1 = (vAddr >> 12) & 0x1FF;

		uPtr *pdpt = getOrCreatePageTable(this->currentLevel4Page, lvl4, true);
		uPtr *pd = getOrCreatePageTable(pdpt, lvl3, true);
		PageTable *pt = reinterpret_cast<PageTable *>(getOrCreatePageTable(pd, lvl2, true));

		pt->entries[lvl1].present = 1;
		pt->entries[lvl1].writeable = (flags >> 1) & 1;
		pt->entries[lvl1].userAccess = (flags >> 2) & 1;
		pt->entries[lvl1].writeThrough = (flags >> 3) & 1;
		pt->entries[lvl1].cacheDisabled = (flags >> 4) & 1;
		pt->entries[lvl1].accessed = (flags >> 5) & 1;
		pt->entries[lvl1].dirty = (flags >> 6) & 1;
		pt->entries[lvl1].size = (flags >> 7) & 1;

		pt->entries[lvl1].address = (pAddr >> 12) & 0xFFFFFFFFFF;
	}

	uPtr*  PagingManager::getOrCreatePageTable(uPtr* parent, u16 index, bool isUser) {
		if (!(parent->entries[index] & 1)) { // Not present
			page_table_t* new_table = alloc_page_table();
			memset(new_table, 0, PAGE_SIZE);
			parent->entries[index] = ((uint64_t)new_table) | 0b11; // Present + Writable
			if (user) parent->entries[index] |= (1 << 2); // User bit
		}
		return (page_table_t*)(parent->entries[index] & ~0xFFFULL);
	}
}