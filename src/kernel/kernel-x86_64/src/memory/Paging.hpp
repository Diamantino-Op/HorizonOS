#ifndef KERNEL_X86_64_PAGING_HPP
#define KERNEL_X86_64_PAGING_HPP

#include "Types.hpp"

#include "memory/CommonPaging.hpp"

namespace kernel::x86_64::memory {
	using namespace common::memory;

    struct __attribute__((packed, aligned(4096))) PageEntry {
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
	struct __attribute__((packed, aligned(4096))) PageTable {
		PageEntry entries [512];
	};

	class PagingManager : CommonPagingManager {
	public:
		PagingManager();

		void loadPageTable();

		void mapPage(uPtr* level4Page, u64 vAddr, u64 pAddr, u8 flags) override;

	private:
		uPtr* getOrCreatePageTable(uPtr* parent, u16 index, bool isUser) override;
	};

	extern "C" void loadPageTableAsm(uPtr *pageTablePointer);
}

#endif