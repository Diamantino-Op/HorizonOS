#include "X86VirtualMemory.hpp"

#include "Main.hpp"
#include "hal/Syscall.hpp"
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

	void PageMap::init(u64 *newPageTable, const u64 newPhysPageTable, AllocContext *ctx, const bool newIsKernel) {
		this->isKernel = newIsKernel;
		this->allocCtx = ctx;

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

		this->physPageTable = newPhysPageTable;
		this->pageTable = newPageTable;

		memset(this->pageTable, 0, pageSize);

		if (this->isKernel) {
			for (u16 i = 256; i < 512; i++) {
				getOrCreatePageTable(this->pageTable, i, 0b00000011, true, false);
			}
		}
	}

	void PageMap::load() const {
		// TODO: Re-Enable
		//Terminal* terminal = CommonMain::getTerminal();

		//terminal->debug("Loading main page table: 0x%.16lx", "VMM", reinterpret_cast<u64 *>(reinterpret_cast<u64>(this->pageTable) - CommonMain::getCurrentHhdm()));

		Asm::writeCr3(this->physPageTable);
	}

	void PageMap::mapPage(const u64 vAddr, const u64 pAddr, const u8 flags, const bool global, const bool noExec) {
		const u32 lvl5 = (vAddr >> 48) & 0x1FF;
		const u32 lvl4 = (vAddr >> 39) & 0x1FF;
		const u32 lvl3 = (vAddr >> 30) & 0x1FF;
		const u32 lvl2 = (vAddr >> 21) & 0x1FF;
		const u32 lvl1 = (vAddr >> 12) & 0x1FF;

		PageTable *pdpt = nullptr;

		if (this->isLevel5Paging) {
			auto *lvl5Table = reinterpret_cast<PageTable *>(getOrCreatePageTable(this->pageTable, lvl5, flags, global, noExec));
			pdpt = reinterpret_cast<PageTable *>(getOrCreatePageTable(reinterpret_cast<uPtr *>(lvl5Table), lvl4, flags, global, noExec));
		} else {
			pdpt = reinterpret_cast<PageTable *>(getOrCreatePageTable(this->pageTable, lvl4, flags, global, noExec));
		}

		auto *pd = reinterpret_cast<PageTable *>(getOrCreatePageTable(reinterpret_cast<uPtr *>(pdpt), lvl3, flags, global, noExec));
		auto *pt = reinterpret_cast<PageTable *>(getOrCreatePageTable(reinterpret_cast<uPtr *>(pd), lvl2, flags, global, noExec));

		pt->entries[lvl1].executeDisable = noExec;
		pt->entries[lvl1].global = global;

		this->setPageFlags(reinterpret_cast<uPtr *>(&pt->entries[lvl1]), flags);

		pt->entries[lvl1].address = (pAddr >> 12) & 0xFFFFFFFFFF;

		if (not isKernel) {
			this->pageTree.insert(vAddr, 1, reinterpret_cast<u64 *>(this->allocCtx), allocateRBTreeNode);
		}
	}

	// TODO: Free pages
	void PageMap::unMapPage(const u64 vAddr) {
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

		if (not isKernel) {
			this->pageTree.remove(vAddr, reinterpret_cast<u64 *>(this->allocCtx), deleteRBTreeNode);
		}
	}

	bool PageMap::protectPage(const u64 vAddr, const u8 prot) {
		const u32 lvl5 = (vAddr >> 48) & 0x1FF;
		const u32 lvl4 = (vAddr >> 39) & 0x1FF;
		const u32 lvl3 = (vAddr >> 30) & 0x1FF;
		const u32 lvl2 = (vAddr >> 21) & 0x1FF;
		const u32 lvl1 = (vAddr >> 12) & 0x1FF;

		PageTable *lvl4Table = nullptr;

		if (this->isLevel5Paging) {
			auto *lvl5Table = reinterpret_cast<PageTable *>(this->pageTable);
			if (!lvl5Table->entries[lvl5].present) {
				return false;
			}

			lvl4Table = reinterpret_cast<PageTable *>((lvl5Table->entries[lvl5].address << 12) + CommonMain::getCurrentHhdm());
		} else {
			lvl4Table = reinterpret_cast<PageTable *>(this->pageTable);
		}

		if (!lvl4Table->entries[lvl4].present) {
			return false;
		}

		auto *lvl3Table = reinterpret_cast<PageTable *>((lvl4Table->entries[lvl4].address << 12) + CommonMain::getCurrentHhdm());
		if (!lvl3Table->entries[lvl3].present) {
			return false;
		}

		PageEntry *targetEntry = nullptr;

		if (lvl3Table->entries[lvl3].size) {
			targetEntry = &lvl3Table->entries[lvl3];
		} else {
			auto *lvl2Table = reinterpret_cast<PageTable *>((lvl3Table->entries[lvl3].address << 12) + CommonMain::getCurrentHhdm());
			if (!lvl2Table->entries[lvl2].present) {
				return false;
			}

			if (lvl2Table->entries[lvl2].size) {
				targetEntry = &lvl2Table->entries[lvl2];
			} else {
				auto *lvl1Table = reinterpret_cast<PageTable *>((lvl2Table->entries[lvl2].address << 12) + CommonMain::getCurrentHhdm());
				if (!lvl1Table->entries[lvl1].present) {
					return false;
				}

				targetEntry = &lvl1Table->entries[lvl1];
			}
		}

		targetEntry->present = prot != PROT_NONE;
		targetEntry->writeable = (prot & PROT_WRITE) != 0;
		targetEntry->userAccess = 1;
		targetEntry->executeDisable = (prot & PROT_EXEC) == 0;

		return true;
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

		if (lvl3Table->entries[lvl3].size) {
			return (lvl3Table->entries[lvl3].address << 12) + (vAddr & 0x3FFFFFFF);
		}

		const auto *lvl2Table = reinterpret_cast<PageTable *>((lvl3Table->entries[lvl3].address << 12) + CommonMain::getCurrentHhdm());
		if (!lvl2Table->entries[lvl2].present) {
			return 0;
		}

		if (lvl2Table->entries[lvl2].size) {
			return (lvl2Table->entries[lvl2].address << 12) + (vAddr & 0x1FFFFF);
		}

		const auto *lvl1Table = reinterpret_cast<PageTable *>((lvl2Table->entries[lvl2].address << 12) + CommonMain::getCurrentHhdm());
		if (!lvl1Table->entries[lvl1].present) {
			return 0;
		}

		return (lvl1Table->entries[lvl1].address << 12) + (vAddr & 0xFFF);
	}

	u64* PageMap::getOrCreatePageTable(u64* parent, const u16 index, const u8 flags, const bool global, const bool noExec) {
		auto *parentTable = reinterpret_cast<PageTable *>(parent);

		if (!parentTable->entries[index].present) {
			auto *newTable = reinterpret_cast<PageTable *>(CommonMain::getInstance()->getPMM()->allocPages(1, false));

			if (!newTable) {
				return nullptr;
			}

			parentTable->entries[index].executeDisable = noExec;
			parentTable->entries[index].global = global;

			this->setPageFlags(reinterpret_cast<u64 *>(&parentTable->entries[index]), flags);

			parentTable->entries[index].address = (reinterpret_cast<u64>(newTable) >> 12) & 0xFFFFFFFFFF;
		} else {
			// TODO: Maybe add more flags
			auto *pageEntry = &parentTable->entries[index];
            pageEntry->writeable |= (flags >> 1) & 1;
            pageEntry->userAccess |= (flags >> 2) & 1;
			parentTable->entries[index].executeDisable &= noExec;
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

	u64 PageMap::getAddr() const {
		return this->physPageTable;
	}
}

