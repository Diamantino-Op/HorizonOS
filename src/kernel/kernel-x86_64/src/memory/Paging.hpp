#ifndef KERNEL_X86_64_PAGING_HPP
#define KERNEL_X86_64_PAGING_HPP

#include "Types.hpp"

namespace x86_64::memory {
    struct PageMapEntry {
    	u8 flags{};
    	u8 availableLow : 3 {};
    	u64 address : 40 {};
		u32 availableHigh : 11 {};
    	u8 executeDisable : 1 {};
    };

    struct PointerTableEntry {

	};

	struct PageDirectoryEntry {

	};

	struct PageTableEntry {

	};
}

#endif