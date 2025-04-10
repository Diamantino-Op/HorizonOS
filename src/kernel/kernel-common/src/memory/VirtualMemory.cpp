#include "VirtualMemory.hpp"

namespace kernel::common::memory {
	template<class T> void PagingManager<T>::handlePageFault(u64 faultAddr, u8 flags) {
		u64 alignedAddr = faultAddr & ~(0x1000 - 1);
		// u64 physAddress = allocatePage();

		this->mapPage(alignedAddr, (u64) nullptr, flags);
	}
}