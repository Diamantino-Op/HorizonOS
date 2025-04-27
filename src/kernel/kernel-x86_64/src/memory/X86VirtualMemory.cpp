#include "X86VirtualMemory.hpp"

#include "Main.hpp"
#include "memory/MainMemory.hpp"
#include "utils/Asm.hpp"

extern limine_paging_mode_request pagingModeRequest;

namespace kernel::common::memory {
	using namespace x86_64;
	using namespace x86_64::memory;
	using namespace x86_64::utils;

	void VirtualMemoryManager::archInit() {
		Terminal* terminal = CommonMain::getTerminal();

		terminal->info("Initializing Virtual Memory Manager...", "VMM");

		terminal->debug("Current HHDM Offset: %lp", "VMM", CommonMain::getCurrentHhdm());

		this->init();
	}

	void PageMap::init(u64 *pageTable) {
		Terminal* terminal = CommonMain::getTerminal();

		if (pagingModeRequest.response != nullptr) {
			// 0 = Page Level 4, 1 = Page Level 5
			if (pagingModeRequest.response->mode == 0) {
				terminal->debug("Current Paging Mode: Level 4", "VMM");

				this->isLevel5Paging = false;
			} else if (pagingModeRequest.response->mode == 1) {
				terminal->debug("Current Paging Mode: Level 5", "VMM");

				this->isLevel5Paging = true;
			}
		}

		this->pageTable = pageTable;
	}

	void PageMap::load() {
		Terminal* terminal = CommonMain::getTerminal();

		terminal->debug("Loading main page table: 0x%.16lx", "VMM", reinterpret_cast<u64 *>(reinterpret_cast<u64>(this->pageTable) - CommonMain::getCurrentHhdm()));

		Asm::writeCr3(reinterpret_cast<u64>(this->pageTable) - CommonMain::getCurrentHhdm());
	}

	void PageMap::mapPage(const u64 vAddr, const u64 pAddr, const u8 flags, const bool noExec) {
		const u32 lvl5 = (vAddr >> 48) & 0x1FF;
		const u32 lvl4 = (vAddr >> 39) & 0x1FF;
		const u32 lvl3 = (vAddr >> 30) & 0x1FF;
		const u32 lvl2 = (vAddr >> 21) & 0x1FF;
		const u32 lvl1 = (vAddr >> 12) & 0x1FF;

		PageTable *pdpt = nullptr;

		if (this->isLevel5Paging) {
			auto *lvl5Table = reinterpret_cast<PageTable *>(getOrCreatePageTable(this->pageTable, lvl5, flags, noExec));
			pdpt = reinterpret_cast<PageTable *>(getOrCreatePageTable(reinterpret_cast<uPtr *>(lvl5Table), lvl4, flags, noExec));
		} else {
			pdpt = reinterpret_cast<PageTable *>(getOrCreatePageTable(this->pageTable, lvl4, flags, noExec));
		}

		auto *pd = reinterpret_cast<PageTable *>(getOrCreatePageTable(reinterpret_cast<uPtr *>(pdpt), lvl3, flags, noExec));
		auto *pt = reinterpret_cast<PageTable *>(getOrCreatePageTable(reinterpret_cast<uPtr *>(pd), lvl2, flags, noExec));

		pt->entries[lvl1].executeDisable = noExec;
		this->setPageFlags(reinterpret_cast<uPtr *>(&pt->entries[lvl1]), flags);

		pt->entries[lvl1].address = (pAddr >> 12) & 0xFFFFFFFFFF;
	}

	void PageMap::unMapPage(const u64 vAddr) const {
		const u32 lvl5 = (vAddr >> 48) & 0x1FF;
		const u32 lvl4 = (vAddr >> 39) & 0x1FF;
		const u32 lvl3 = (vAddr >> 30) & 0x1FF;
		const u32 lvl2 = (vAddr >> 21) & 0x1FF;
		const u32 lvl1 = (vAddr >> 12) & 0x1FF;

		const PageTable *lvl4Table = nullptr;

		if (this->isLevel5Paging) {
			const auto *lvl5Table = reinterpret_cast<PageTable *>(this->pageTable);
			if (!lvl5Table->entries[lvl5].present) {
				return;
			}

			lvl4Table = reinterpret_cast<PageTable *>((lvl5Table->entries[lvl5].address << 12) + CommonMain::getCurrentHhdm());
		} else {
			lvl4Table = reinterpret_cast<PageTable *>(this->pageTable);
		}

		if (!lvl4Table->entries[lvl4].present) {
			return;
		}

		const auto *lvl3Table = reinterpret_cast<PageTable *>((lvl4Table->entries[lvl4].address << 12) + CommonMain::getCurrentHhdm());
		if (!lvl3Table->entries[lvl3].present) {
			return;
		}

		const auto *lvl2Table = reinterpret_cast<PageTable *>((lvl3Table->entries[lvl3].address << 12) + CommonMain::getCurrentHhdm());
		if (!lvl2Table->entries[lvl2].present) {
			return;
		}

		auto *lvl1Table = reinterpret_cast<PageTable *>((lvl2Table->entries[lvl2].address << 12) + CommonMain::getCurrentHhdm());
		if (lvl1Table->entries[lvl1].present) {
			memset(&lvl1Table->entries[lvl1], 0, sizeof(lvl1Table->entries[lvl1]));
		}
	}

	u64 PageMap::getPhysAddress(const u64 vAddr) const {
		const u32 lvl5 = (vAddr >> 48) & 0x1FF;
		const u32 lvl4 = (vAddr >> 39) & 0x1FF;
		const u32 lvl3 = (vAddr >> 30) & 0x1FF;
		const u32 lvl2 = (vAddr >> 21) & 0x1FF;
		const u32 lvl1 = (vAddr >> 12) & 0x1FF;

		const PageTable *lvl4Table = nullptr;

		if (this->isLevel5Paging) {
			const auto *lvl5Table = reinterpret_cast<PageTable *>(this->pageTable);
			if (!lvl5Table->entries[lvl5].present) {
				return 0;
			}

			lvl4Table = reinterpret_cast<PageTable *>((lvl5Table->entries[lvl5].address << 12) + CommonMain::getCurrentHhdm());
		} else {
			lvl4Table = reinterpret_cast<PageTable *>(this->pageTable);
		}

		if (!lvl4Table->entries[lvl4].present) {
			return 0;
		}

		const auto *lvl3Table = reinterpret_cast<PageTable *>((lvl4Table->entries[lvl4].address << 12) + CommonMain::getCurrentHhdm());
		if (!lvl3Table->entries[lvl3].present) {
			return 0;
		}

		const auto *lvl2Table = reinterpret_cast<PageTable *>((lvl3Table->entries[lvl3].address << 12) + CommonMain::getCurrentHhdm());
		if (!lvl2Table->entries[lvl2].present) {
			return 0;
		}

		const auto *lvl1Table = reinterpret_cast<PageTable *>((lvl2Table->entries[lvl2].address << 12) + CommonMain::getCurrentHhdm());
		if (!lvl1Table->entries[lvl1].present) {
			return 0;
		}

		return lvl1Table->entries[lvl1].address << 12;
	}

	u64* PageMap::getOrCreatePageTable(u64* parent, const u16 index, const u8 flags, const bool noExec) {
		auto *parentTable = reinterpret_cast<PageTable *>(parent);

		if (!parentTable->entries[index].present) {
			auto *newTable = reinterpret_cast<PageTable *>(CommonMain::getInstance()->getPMM()->allocPages(1, false));

			if (!newTable) {
				return nullptr;
			}

			parentTable->entries[index].executeDisable = noExec;
			this->setPageFlags(reinterpret_cast<u64 *>(&parentTable->entries[index]), flags);

			parentTable->entries[index].address = (reinterpret_cast<u64>(newTable) >> 12) & 0xFFFFFFFFFF;
		} else {
			this->setPageFlags(reinterpret_cast<u64 *>(&parentTable->entries[index]), flags);
		}

		return reinterpret_cast<u64 *>((parentTable->entries[index].address << 12) + CommonMain::getCurrentHhdm());
	}

	void PageMap::setPageFlags(u64 *pageAddr, const u8 flags) {
		auto *pageEntry = reinterpret_cast<PageEntry *>(pageAddr);

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