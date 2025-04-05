#include "Paging.hpp"

namespace kernel::x86_64::memory {
	 PagingManager::PagingManager() {
	 	for (u32 i = 0; i < 512; i++) {
	 		this->pageDirectoryTables.table[i].flags = PageTableFlags::PAGE_READ_WRITE | PageTableFlags::PAGE_PRESENT | PageTableFlags::PAGE_SIZE;
	 	}

	 	this->pageDirectoryPointerTable.table[0].flags = PageTableFlags::PAGE_PRESENT;
	 	this->pageDirectoryPointerTable.table[0].address = reinterpret_cast<usize>(&this->pageDirectoryTables) >> 12;

		initPagingAsm(reinterpret_cast<usize>(&this->pageDirectoryPointerTable));
	}
}