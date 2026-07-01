#include "X86VirtualMemory.hpp"

#include "Main.hpp"
#include "hal/Syscall.hpp"
#include "memory/MainMemory.hpp"
#include "utils/Asm.hpp"
#include "utils/CpuId.hpp"

namespace kernel::common::memory {
	using namespace x86_64;
	using namespace x86_64::memory;
	using namespace x86_64::utils;

	namespace {
		constexpr u64 pageEntryAddrMask = 0x000FFFFFFFFFF000;
		constexpr u64 pageEntryWriteThrough = 1ULL << 3;
		constexpr u64 pageEntryCacheDisabled = 1ULL << 4;
		constexpr u64 pageEntryPat4KiB = 1ULL << 7;
		constexpr u64 pageEntryPatHuge = 1ULL << 12;

		bool isAligned(const u64 value, const u64 alignment) {
			return (value & (alignment - 1)) == 0;
		}
	}

	void VirtualMemoryManager::archInit() {
		Terminal* terminal = CommonMain::getTerminal();

		terminal->info("Initializing Virtual Memory Manager...", "VMM");

		terminal->debug("Current HHDM Offset: %lp", "VMM", CommonMain::getCurrentHhdm());

		this->init();
	}

	void VirtualMemoryManager::initPAT() {
		Terminal* terminal = CommonMain::getTerminal();

		if (not CpuId::hasPAT()) {
			terminal->warn("PAT not supported!", "VMM");

			return;
		}

		const u64 pat = patEntry(0, PAT_TYPE_WB) | patEntry(1, PAT_TYPE_WC) | patEntry(2, PAT_TYPE_UCM) | patEntry(3, PAT_TYPE_UC) | patEntry(4, PAT_TYPE_WB) | patEntry(5, PAT_TYPE_WT) | patEntry(6, PAT_TYPE_UCM) | patEntry(7, PAT_TYPE_UC);

		Asm::wrmsr(IA32_PAT, pat);
	}

	auto VirtualMemoryManager::patEntry(const u8 index, const u8 type) -> u64 {
		return (static_cast<u64>(type)) << (index * 8);
	}

	void PageMap::init(u64 *newPageTable, const u64 newPhysPageTable, AllocContext *ctx, const bool newIsKernel) {
		this->isKernel = newIsKernel;
		this->allocCtx = ctx;

		Terminal* terminal = CommonMain::getTerminal();

		if (VirtualAllocator::isPagingLvl5) {
			terminal->debug("Current Paging Mode: Level 5", "VMM");
		} else {
			terminal->debug("Current Paging Mode: Level 4", "VMM");
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

	void PageMap::mapPage(const u64 vAddr, const u64 pAddr, const u8 flags, const bool global, const bool noExec, const PageCacheMode cacheMode) {
		const u32 lvl5 = (vAddr >> 48) & 0x1FF;
		const u32 lvl4 = (vAddr >> 39) & 0x1FF;
		const u32 lvl3 = (vAddr >> 30) & 0x1FF;
		const u32 lvl2 = (vAddr >> 21) & 0x1FF;
		const u32 lvl1 = (vAddr >> 12) & 0x1FF;

		PageTable *pdpt = nullptr;

		if (VirtualAllocator::isPagingLvl5) {
			auto *lvl5Table = reinterpret_cast<PageTable *>(getOrCreatePageTable(this->pageTable, lvl5, flags, global, noExec));
			if (lvl5Table == nullptr) {
				return;
			}

			pdpt = reinterpret_cast<PageTable *>(getOrCreatePageTable(reinterpret_cast<uPtr *>(lvl5Table), lvl4, flags, global, noExec));
		} else {
			pdpt = reinterpret_cast<PageTable *>(getOrCreatePageTable(this->pageTable, lvl4, flags, global, noExec));
		}

		if (pdpt == nullptr) {
			return;
		}

		auto *pd = reinterpret_cast<PageTable *>(getOrCreatePageTable(reinterpret_cast<uPtr *>(pdpt), lvl3, flags, global, noExec));
		if (pd == nullptr) {
			return;
		}

		auto *pt = reinterpret_cast<PageTable *>(getOrCreatePageTable(reinterpret_cast<uPtr *>(pd), lvl2, flags, global, noExec));
		if (pt == nullptr) {
			return;
		}

		pt->entries[lvl1].executeDisable = noExec;
		pt->entries[lvl1].global = global;

		this->setPageFlags(reinterpret_cast<uPtr *>(&pt->entries[lvl1]), flags);

		pt->entries[lvl1].address = (pAddr >> 12) & 0xFFFFFFFFFF;
		this->setPageCacheMode(reinterpret_cast<uPtr *>(&pt->entries[lvl1]), cacheMode, false);

		if (not isKernel) {
			this->pageTree.insert(vAddr, 1, reinterpret_cast<u64 *>(this->allocCtx), allocateRBTreeNode);
		}
	}

	bool PageMap::mapHugePage(const u64 vAddr, const u64 pAddr, const u64 hugeSize, const u8 flags, const bool global, const bool noExec, const PageCacheMode cacheMode) {
		if ((hugeSize != hugePageSize2MiB and hugeSize != hugePageSize1GiB) or not isAligned(vAddr, hugeSize) or not isAligned(pAddr, hugeSize)) {
			return false;
		}

		const u32 lvl5 = (vAddr >> 48) & 0x1FF;
		const u32 lvl4 = (vAddr >> 39) & 0x1FF;
		const u32 lvl3 = (vAddr >> 30) & 0x1FF;
		const u32 lvl2 = (vAddr >> 21) & 0x1FF;

		PageTable *pdpt = nullptr;

		if (VirtualAllocator::isPagingLvl5) {
			auto *lvl5Table = reinterpret_cast<PageTable *>(getOrCreatePageTable(this->pageTable, lvl5, flags, global, noExec));
			if (lvl5Table == nullptr) {
				return false;
			}

			pdpt = reinterpret_cast<PageTable *>(getOrCreatePageTable(reinterpret_cast<uPtr *>(lvl5Table), lvl4, flags, global, noExec));
		} else {
			pdpt = reinterpret_cast<PageTable *>(getOrCreatePageTable(this->pageTable, lvl4, flags, global, noExec));
		}

		if (pdpt == nullptr) {
			return false;
		}

		PageEntry *targetEntry = nullptr;

		if (hugeSize == hugePageSize1GiB) {
			targetEntry = &pdpt->entries[lvl3];
		} else {
			auto *pd = reinterpret_cast<PageTable *>(getOrCreatePageTable(reinterpret_cast<uPtr *>(pdpt), lvl3, flags, global, noExec));

			if (pd == nullptr) {
				return false;
			}

			targetEntry = &pd->entries[lvl2];
		}

		if (targetEntry->present) {
			return false;
		}

		targetEntry->executeDisable = noExec;
		targetEntry->global = global;

		this->setPageFlags(reinterpret_cast<uPtr *>(targetEntry), flags);

		targetEntry->size = 1;
		targetEntry->address = (pAddr >> 12) & 0xFFFFFFFFFF;
		this->setPageCacheMode(reinterpret_cast<uPtr *>(targetEntry), cacheMode, true);

		if (not isKernel) {
			this->pageTree.insert(vAddr, hugeSize / pageSize, reinterpret_cast<u64 *>(this->allocCtx), allocateRBTreeNode);
		}

		return true;
	}

	void PageMap::unMapPage(const u64 vAddr, const bool freePage) {
		const u32 lvl5 = (vAddr >> 48) & 0x1FF;
		const u32 lvl4 = (vAddr >> 39) & 0x1FF;
		const u32 lvl3 = (vAddr >> 30) & 0x1FF;
		const u32 lvl2 = (vAddr >> 21) & 0x1FF;
		const u32 lvl1 = (vAddr >> 12) & 0x1FF;

		const PageTable *lvl4Table = nullptr;

		if (VirtualAllocator::isPagingLvl5) {
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

		auto *lvl3Table = reinterpret_cast<PageTable *>((lvl4Table->entries[lvl4].address << 12) + CommonMain::getCurrentHhdm());
		if (!lvl3Table->entries[lvl3].present) {
			return;
		}

		if (lvl3Table->entries[lvl3].size) {
			if (freePage) {
				const u64 physAddr = getEntryPhysAddress(&lvl3Table->entries[lvl3], true);

				memset(&lvl3Table->entries[lvl3], 0, sizeof(lvl3Table->entries[lvl3]));

				if (physAddr != 0) {
					CommonMain::getInstance()->getPMM()->freePagesPhys(reinterpret_cast<u64 *>(physAddr), hugePageSize1GiB / pageSize);
				}
			} else {
				memset(&lvl3Table->entries[lvl3], 0, sizeof(lvl3Table->entries[lvl3]));
			}

			if (not isKernel) {
				this->pageTree.remove(vAddr, reinterpret_cast<u64 *>(this->allocCtx), deleteRBTreeNode);
			}

			return;
		}

		auto *lvl2Table = reinterpret_cast<PageTable *>((lvl3Table->entries[lvl3].address << 12) + CommonMain::getCurrentHhdm());
		if (!lvl2Table->entries[lvl2].present) {
			return;
		}

		if (lvl2Table->entries[lvl2].size) {
			if (freePage) {
				const u64 physAddr = getEntryPhysAddress(&lvl2Table->entries[lvl2], true);

				memset(&lvl2Table->entries[lvl2], 0, sizeof(lvl2Table->entries[lvl2]));

				if (physAddr != 0) {
					CommonMain::getInstance()->getPMM()->freePagesPhys(reinterpret_cast<u64 *>(physAddr), hugePageSize2MiB / pageSize);
				}
			} else {
				memset(&lvl2Table->entries[lvl2], 0, sizeof(lvl2Table->entries[lvl2]));
			}

			if (not isKernel) {
				this->pageTree.remove(vAddr, reinterpret_cast<u64 *>(this->allocCtx), deleteRBTreeNode);
			}

			return;
		}

		auto *lvl1Table = reinterpret_cast<PageTable *>((lvl2Table->entries[lvl2].address << 12) + CommonMain::getCurrentHhdm());
		if (lvl1Table->entries[lvl1].present) {
			if (freePage) {
				const u64 physAddr = getEntryPhysAddress(&lvl1Table->entries[lvl1], false);
				const bool hadAddr = lvl1Table->entries[lvl1].address != 0;

				memset(&lvl1Table->entries[lvl1], 0, sizeof(lvl1Table->entries[lvl1]));

				if (hadAddr) {
					CommonMain::getInstance()->getPMM()->freePagesPhys(reinterpret_cast<u64 *>(physAddr), 1);
				}
			} else {
				memset(&lvl1Table->entries[lvl1], 0, sizeof(lvl1Table->entries[lvl1]));
			}
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

		const PageTable *lvl4Table = nullptr;

		if (VirtualAllocator::isPagingLvl5) {
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

		if (VirtualAllocator::isPagingLvl5) {
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
			return getEntryPhysAddress(&lvl3Table->entries[lvl3], true) + (vAddr & (hugePageSize1GiB - 1));
		}

		const auto *lvl2Table = reinterpret_cast<PageTable *>((lvl3Table->entries[lvl3].address << 12) + CommonMain::getCurrentHhdm());
		if (!lvl2Table->entries[lvl2].present) {
			return 0;
		}

		if (lvl2Table->entries[lvl2].size) {
			return getEntryPhysAddress(&lvl2Table->entries[lvl2], true) + (vAddr & (hugePageSize2MiB - 1));
		}

		const auto *lvl1Table = reinterpret_cast<PageTable *>((lvl2Table->entries[lvl2].address << 12) + CommonMain::getCurrentHhdm());
		if (!lvl1Table->entries[lvl1].present) {
			return 0;
		}

		return getEntryPhysAddress(&lvl1Table->entries[lvl1], false) + (vAddr & 0xFFF);
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
			if (parentTable->entries[index].size) {
				return nullptr;
			}

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

	void PageMap::setPageCacheMode(u64 *pageAddr, const PageCacheMode cacheMode, const bool isHugePage) {
		u64 cacheFlags = 0;

		switch (cacheMode) {
			case PageCacheMode::WriteBack:
				cacheFlags = 0;
				break;

			case PageCacheMode::WriteCombining:
				cacheFlags = pageEntryWriteThrough;
				break;

			case PageCacheMode::Uncacheable:
				cacheFlags = pageEntryWriteThrough | pageEntryCacheDisabled;
				break;

			case PageCacheMode::WriteThrough:
				cacheFlags = pageEntryWriteThrough | (isHugePage ? pageEntryPatHuge : pageEntryPat4KiB);
				break;
		}

		const u64 patBit = isHugePage ? pageEntryPatHuge : pageEntryPat4KiB;
		*pageAddr = (*pageAddr & ~(pageEntryWriteThrough | pageEntryCacheDisabled | patBit)) | cacheFlags;
	}

	u64 PageMap::getEntryPhysAddress(const void *entry, const bool isHugePage) {
		u64 physAddr = *reinterpret_cast<const u64 *>(entry) & pageEntryAddrMask;

		if (isHugePage) {
			physAddr &= ~pageEntryPatHuge;
		}

		return physAddr;
	}

	u64 PageMap::getAddr() const {
		return this->physPageTable;
	}

	void PageMap::invPg(const u64 page) {
		Asm::invalidatePage(page);
	}
}

