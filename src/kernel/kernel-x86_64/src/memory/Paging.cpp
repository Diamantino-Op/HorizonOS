#include "Paging.hpp"

namespace kernel::x86_64::memory {
	 PagingManager::PagingManager() {
	 	auto tempPaging = Level4PageTable {};

		this->paging = reinterpret_cast<uPtr *>(&tempPaging);
	}

	void PagingManager::loadPageTable() {
		loadPageTableAsm(this->paging);
	}
}