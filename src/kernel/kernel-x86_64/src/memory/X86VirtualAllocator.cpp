#include "memory/VirtualAllocator.hpp"

#include "CommonMain.hpp"
#include "X86VirtualMemory.hpp"
#include "memory/MainMemory.hpp"

namespace kernel::common::memory {
	using namespace x86_64::memory;

	// TODO: Optimize this shit
	void VirtualAllocator::destroyContext(AllocContext *ctx) {
		for (u64 i = 0; i < ctx->heapSize; i += pageSize) {
			const auto virtAddress = reinterpret_cast<u64>(ctx->heapStart) + i;

			memset(reinterpret_cast<u64 *>(virtAddress), 0, pageSize);

			ctx->pageMap.unMapPage(virtAddress);

			CommonMain::getInstance()->getPMM()->freePages(reinterpret_cast<u64 *>(virtAddress), 1);
		}

		for (u16 page5Level = 0; page5Level < 512; page5Level++) {
			auto *level5Table = reinterpret_cast<PageTable *>(ctx->pageMap.getPageTable());

			if (auto *level5Entry = &level5Table->entries[page5Level]; level5Entry != nullptr) {
				for (u16 page4Level = 0; page4Level < 512; page4Level++) {
					auto *level4Table = reinterpret_cast<PageTable *>((level5Entry->address << 12) + CommonMain::getCurrentHhdm());

					if (auto *level4Entry = &level4Table->entries[page4Level]; level4Entry != nullptr) {
						for (u16 page3Level = 0; page3Level < 512; page3Level++) {
							auto *level3Table = reinterpret_cast<PageTable *>((level4Entry->address << 12) + CommonMain::getCurrentHhdm());

							if (auto *level3Entry = &level3Table->entries[page3Level]; level3Entry != nullptr) {
								for (u16 page2Level = 0; page2Level < 512; page2Level++) {
									auto *level2Table = reinterpret_cast<PageTable *>((level3Entry->address << 12) + CommonMain::getCurrentHhdm());

									if (auto *level2Entry = &level2Table->entries[page2Level]; level2Entry != nullptr) {
										if (ctx->pageMap.level5Paging()) {
											for (u16 page1Level = 0; page1Level < 512; page1Level++) {
												auto *level1Table = reinterpret_cast<PageTable *>((level2Entry->address << 12) + CommonMain::getCurrentHhdm());

												if (auto *level1Entry = &level1Table->entries[page1Level]; level1Entry != nullptr) {
													CommonMain::getInstance()->getPMM()->freePages(reinterpret_cast<u64 *>(level1Entry), 1);
												}
											}
										}

										CommonMain::getInstance()->getPMM()->freePages(reinterpret_cast<u64 *>(level2Entry), 1);
									}
								}

								CommonMain::getInstance()->getPMM()->freePages(reinterpret_cast<u64 *>(level3Entry), 1);
							}
						}

						CommonMain::getInstance()->getPMM()->freePages(reinterpret_cast<u64 *>(level4Entry), 1);
					}
				}

				CommonMain::getInstance()->getPMM()->freePages(reinterpret_cast<u64 *>(level5Entry), 1);
			}
		}

		CommonMain::getInstance()->getPMM()->freePages(ctx->pageMap.getPageTable(), 1);

		delete ctx;
	}

	void VirtualAllocator::shareKernelPages(const AllocContext *ctx) {
		const auto *kernelTable = reinterpret_cast<PageTable *>(CommonMain::getInstance()->getKernelAllocContext()->pageMap.getPageTable());

		for (u64 i = 0; i < 256; i++) {
			auto *table = reinterpret_cast<PageTable *>(ctx->pageMap.getPageTable());

			table->entries[256 + i] = kernelTable->entries[256 + i];
		}
	}
}