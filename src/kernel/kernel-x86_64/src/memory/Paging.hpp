#ifndef KERNEL_X86_64_PAGING_HPP
#define KERNEL_X86_64_PAGING_HPP

#include "Types.hpp"

namespace kernel::x86_64::memory {
    struct __attribute__((packed, aligned(8))) PageEntry {
    	u8 present : 1 {};
    	u8 writeable : 1 {};
    	u8 user_access : 1 {};
    	u8 write_through : 1 {};
    	u8 cache_disabled : 1 {};
    	u8 accessed : 1 {};
    	u8 dirty : 1 {};
    	u8 size : 1 {};
    	u8 global : 1 {};
    	u8 availableLow : 3 {};
    	u64 address : 40 {};
		u8 availableHigh : 7 {};
    	u8 pk : 4 {};
    	u8 executeDisable : 1 {};

    	void clearAuto();

    	void clearNoFree();
    };

	class X86PageTable {

	};

	class PagingManager {
	public:
		PagingManager();
	};

	extern "C" void initPagingAsm(u64 pageTablePointer);
}

#endif