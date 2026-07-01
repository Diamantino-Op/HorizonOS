#include "memory/VirtualAllocator.hpp"

#include "CommonMain.hpp"
#include "X86VirtualMemory.hpp"
#include "utils/Asm.hpp"

namespace kernel::common::memory {
	using namespace x86_64::memory;
	using namespace x86_64::utils;

	void VirtualAllocator::freePageTableChildren(const u64 *tableAddr, const bool level5Paging, const u8 depth, const u64 skipPhysA, const u64 skipPhysB) {
		const auto *table = reinterpret_cast<const PageTable *>(tableAddr);
		const u8 leafTableDepth = level5Paging ? 4 : 3;

		if (depth >= leafTableDepth) {
			return;
		}

		const u16 entryCount = depth == 0 ? rootUserEntryCount : tableEntryCount;

		for (u16 index = 0; index < entryCount; index++) {
			const auto &entry = table->entries[index];

			if (!entry.present) {
				continue;
			}

			const u64 entryPhysAddress = entry.address << pageShift;

			if (entry.size) {
				usize pageCount = 0;

				if (depth == leafTableDepth - 2) {
					pageCount = hugePage1GCount;
				} else if (depth == leafTableDepth - 1) {
					pageCount = hugePage2MCount;
				}

				if (pageCount == 0) {
					continue;
				}

				CommonMain::getInstance()->getPMM()->freePagesPhys(reinterpret_cast<u64 *>(entryPhysAddress), pageCount);

				continue;
			}

			if (depth == leafTableDepth - 1) {
				if (entryPhysAddress != skipPhysA && entryPhysAddress != skipPhysB) {
					CommonMain::getInstance()->getPMM()->freePagesPhys(reinterpret_cast<u64 *>(entryPhysAddress), 1);
				}

				continue;
			}

			freePageTableChildren(reinterpret_cast<u64 *>(entryPhysAddress + CommonMain::getCurrentHhdm()), level5Paging, depth + 1, skipPhysA, skipPhysB);

			CommonMain::getInstance()->getPMM()->freePagesPhys(reinterpret_cast<u64 *>(entryPhysAddress), 1);
		}
	}

	void VirtualAllocator::destroyContext(AllocContext *ctx) {
		if (ctx == nullptr) {
			return;
		}

		const u64 pageTablePhys = ctx->pageMap.getAddr();
		const u64 ctxPhys = ctx->pageMap.getPhysAddress(reinterpret_cast<u64>(ctx));

		freePageTableChildren(ctx->pageMap.getPageTable(), isPagingLvl5, 0, pageTablePhys, ctxPhys);

		CommonMain::getInstance()->getKernelAllocContext()->pageMap.load();

		CommonMain::getInstance()->getPMM()->freePagesPhys(reinterpret_cast<u64 *>(pageTablePhys), 1);
		CommonMain::getInstance()->getPMM()->freePagesPhys(reinterpret_cast<u64 *>(ctxPhys), 1);
	}

	void VirtualAllocator::shareKernelPages(const AllocContext *ctx) {
		// TODO: Update pages if kernel pages are updated

		const auto *kernelTable = reinterpret_cast<const PageTable *>(CommonMain::getInstance()->getKernelAllocContext()->pageMap.getPageTable());
		auto *table = reinterpret_cast<PageTable *>(ctx->pageMap.getPageTable());

		for (u64 i = 0; i < rootUserEntryCount; i++) {
			table->entries[rootUserEntryCount + i] = kernelTable->entries[rootUserEntryCount + i];
		}
	}

	u64 VirtualAllocator::startCreateArch() {
		const u64 currCr3 = Asm::readCr3();

		CommonMain::getInstance()->getKernelAllocContext()->pageMap.load();

		return currCr3;
	}

	void VirtualAllocator::endCreateArch(const u64 value) {
		Asm::writeCr3(value);
	}
}
