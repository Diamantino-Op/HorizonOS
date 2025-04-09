#include "CommonPaging.hpp"

namespace kernel::common::memory {
	void CommonPagingManager::handlePageFault(u64 faultAddr, u8 flags) {
		u64 alignedAddr = faultAddr & ~(0x1000 - 1);
		// u64 physAddress = allocatePage();

		this->mapPage(currentLevel4Page, alignedAddr, (u64) nullptr, flags);
	}
}