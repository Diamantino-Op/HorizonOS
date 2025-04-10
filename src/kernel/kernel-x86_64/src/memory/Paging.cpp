#include "Paging.hpp"

#include "memory/MainMemory.hpp"

namespace kernel::common::memory {
	using namespace x86_64::memory;

	void PagingManager::loadPageTable() {
		loadPageTableAsm(this->currentLevel4Page);
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

	uPtr* PagingManager::getOrCreatePageTable(uPtr* parent, u16 index, bool isUser) {
		PageTable *parentTable = reinterpret_cast<PageTable *>(parent);

		if (!parentTable->entries[index].present) { // Not present
			PageTable* newTable = alloc_page_table();
			memset(newTable, 0, pageSize);

			//TODO: Maybe copy the other fields too
			parentTable->entries[index].writeable = 1;
			parentTable->entries[index].present = 1;
			parentTable->entries[index].address = (reinterpret_cast<u64>(newTable) >> 12) & 0xFFFFFFFFFF; // Present + Writable

			if (isUser) {
				parentTable->entries[index].userAccess = 1;
			}
		}

		return reinterpret_cast<uPtr *>(parentTable->entries[index].address);
	}
}