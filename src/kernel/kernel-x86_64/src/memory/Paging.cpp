#include "Paging.hpp"

namespace kernel::x86_64::memory {
	 PagingManager::PagingManager() {
	 	for (u32 i = 0; i < 4; i++) {
	 		for (u32 j = 0; j < 1024; j++) {
	 			this->pageDirectoryTables[i].table[j].flags = PageTableFlags::PAGE_READ_WRITE | PageTableFlags::PAGE_PRESENT | PageTableFlags::PAGE_SIZE;
	 		}

	 		this->pageDirectoryPointerTable.table[i].flags = PageTableFlags::PAGE_PRESENT;
	 		this->pageDirectoryPointerTable.table[i].address = reinterpret_cast<usize>(&this->pageDirectoryTables[i]) >> 12;
	 	}

		initPagingAsm(reinterpret_cast<usize>(&this->pageDirectoryPointerTable));
	}
}