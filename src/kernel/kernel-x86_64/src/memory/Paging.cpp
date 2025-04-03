#include "Paging.hpp"

namespace kernel::x86_64::memory {
	 PagingManager::PagingManager() {
		for (u32 i = 0; i < 1024; i++) {
			pageDirectoryTable[i].flags = PageTableFlags::PAGE_READ_WRITE;

			pageTable[i].flags = PageTableFlags::PAGE_READ_WRITE | PageTableFlags::PAGE_PRESENT;
			pageTable[i].address = i * 0x1;
		}

	 	pageDirectoryTable[0].address = reinterpret_cast<u64>(pageTable);

		initPagingAsm(reinterpret_cast<usize>(&this->pageDirectoryTable));
	}
}