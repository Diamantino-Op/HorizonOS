#include "X86VirtualMemory.hpp"

#include "Main.hpp"
#include "memory/MainMemory.hpp"

__attribute__((used, section(".limine_requests")))
static volatile limine_hhdm_request hhdmRequest = {
	.id = LIMINE_HHDM_REQUEST,
	.revision = 0,
	.response = nullptr,
};

__attribute__((used, section(".limine_requests")))
static volatile limine_paging_mode_request pagingModeRequest = {
	.id = LIMINE_PAGING_MODE_REQUEST,
	.revision = 0,
	.response = nullptr,
	.mode = 1,
	.max_mode = 1,
	.min_mode = 0,
};

namespace kernel::common::memory {
	using namespace x86_64;
	using namespace x86_64::memory;

	void VirtualMemoryManager::archInit() {
		Terminal* terminal = CommonMain::getTerminal();

		terminal->printf("Initializing Virtual Memory Manager...\n");

		if (hhdmRequest.response != nullptr) {
			this->currentHhdm = hhdmRequest.response->offset;
		}

		terminal->printf("Current HHDM Offset: %lp\n", this->currentHhdm);

		if (pagingModeRequest.response != nullptr) {
			// 0 = Page Level 4, 1 = Page Level 5
			if (pagingModeRequest.response->mode == 0) {
				terminal->printf("Current Paging Mode: Level 4\n");

				this->isLevel5Paging = false;
			} else if (pagingModeRequest.response->mode == 1) {
				terminal->printf("Current Paging Mode: Level 5\n");

				this->isLevel5Paging = true;
			}
		}

		// this->currentMainPage = PageTable(); TODO: Replace with pmm alloc

		this->init();
	}

	void VirtualMemoryManager::loadPageTable() {
		Terminal* terminal = CommonMain::getTerminal();

		terminal->printf("Loading main page table: %l", this->currentMainPage - this->currentHhdm);

		loadPageTableAsm(this->currentMainPage - this->currentHhdm);
	}

	void VirtualMemoryManager::mapPage(u64 vAddr, u64 pAddr, u8 flags, bool noExec) {
		u32 lvl4 = (vAddr >> 39) & 0x1FF;
		u32 lvl3 = (vAddr >> 30) & 0x1FF;
		u32 lvl2 = (vAddr >> 21) & 0x1FF;
		u32 lvl1 = (vAddr >> 12) & 0x1FF;

		PageTable *pdpt = reinterpret_cast<PageTable *>(getOrCreatePageTable(this->currentMainPage, lvl4, flags, noExec));
		PageTable *pd = reinterpret_cast<PageTable *>(getOrCreatePageTable(reinterpret_cast<uPtr *>(pdpt), lvl3, flags, noExec));
		PageTable *pt = reinterpret_cast<PageTable *>(getOrCreatePageTable(reinterpret_cast<uPtr *>(pd), lvl2, flags, noExec));

		pt->entries[lvl1].executeDisable = noExec;
		this->setPageFlags(reinterpret_cast<uPtr *>(&pt->entries[lvl1]), flags);

		pt->entries[lvl1].address = (pAddr >> 12) & 0xFFFFFFFFFF;
	}

	void VirtualMemoryManager::unMapPage(u64 vAddr) {
		u32 lvl4 = (vAddr >> 39) & 0x1FF;
		u32 lvl3 = (vAddr >> 30) & 0x1FF;
		u32 lvl2 = (vAddr >> 21) & 0x1FF;
		u32 lvl1 = (vAddr >> 12) & 0x1FF;

		PageTable *lvl4Table = reinterpret_cast<PageTable *>(this->currentMainPage);

		if (!lvl4Table->entries[lvl4].present) {
			return;
		}

		PageTable *lvl3Table = reinterpret_cast<PageTable *>((lvl4Table->entries[lvl4].address << 12) + this->currentHhdm);
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

	uPtr* VirtualMemoryManager::getOrCreatePageTable(uPtr* parent, u16 index, u8 flags, bool noExec) {
		PageTable *parentTable = reinterpret_cast<PageTable *>(parent);

		if (!parentTable->entries[index].present) {
			PageTable* newTable = nullptr; // alloc_page_table()
			memset(newTable + this->currentHhdm, 0, pageSize);

			parentTable->entries[index].executeDisable = noExec;
			this->setPageFlags(reinterpret_cast<uPtr *>(&parentTable->entries[index]), flags);

			parentTable->entries[index].address = (reinterpret_cast<u64>(newTable) >> 12) & 0xFFFFFFFFFF;
		} else {
			this->setPageFlags(reinterpret_cast<uPtr *>(&parentTable->entries[index]), flags);
		}

		return reinterpret_cast<uPtr *>((parentTable->entries[index].address << 12) + this->currentHhdm);
	}

	void VirtualMemoryManager::setPageFlags(uPtr *pageAddr, u8 flags) {
		PageEntry *pageEntry = reinterpret_cast<PageEntry *>(pageAddr);

		pageEntry->present = flags & 1;
		pageEntry->writeable = (flags >> 1) & 1;
		pageEntry->userAccess = (flags >> 2) & 1;
		pageEntry->writeThrough = (flags >> 3) & 1;
		pageEntry->cacheDisabled = (flags >> 4) & 1;
		pageEntry->accessed = (flags >> 5) & 1;
		pageEntry->dirty = (flags >> 6) & 1;
		pageEntry->size = (flags >> 7) & 1;
	}
}