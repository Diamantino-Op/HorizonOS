#ifndef KERNEL_X86_64_X86VIRTUALMEMORY_HPP
#define KERNEL_X86_64_X86VIRTUALMEMORY_HPP

#include "Types.hpp"

#include "memory/VirtualMemory.hpp"

namespace kernel::x86_64::memory {
	using namespace common::memory;

    struct __attribute__((packed)) PageEntry {
    	u8 present : 1 {};
    	u8 writeable : 1 {};
    	u8 userAccess : 1 {};
    	u8 writeThrough : 1 {};
    	u8 cacheDisabled : 1 {};
    	u8 accessed : 1 {};
    	u8 dirty : 1 {};
    	u8 size : 1 {};
    	u8 global : 1 {};
    	u8 availableLow : 3 {};
    	u64 address : 40 {};
		u8 availableHigh : 7 {};
    	u8 pk : 4 {};
    	u8 executeDisable : 1 {};
    };

	// TODO: Move to common file
	struct __attribute__((packed, aligned(pageSize))) PageTable {
		PageEntry entries [512];
	};

	extern "C" void loadPageTableAsm(uPtr *pageTablePointer);
}

#endif