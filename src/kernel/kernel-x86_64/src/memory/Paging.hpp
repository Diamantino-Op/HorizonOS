#ifndef KERNEL_X86_64_PAGING_HPP
#define KERNEL_X86_64_PAGING_HPP

#include "Types.hpp"

namespace kernel::x86_64::memory {
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
	struct __attribute__((packed, aligned(4096))) Level1PageTable {
		PageEntry entries [512];
	};

	// TODO: Move to common file
	struct __attribute__((packed, aligned(4096))) Level2PageTable {
		PageEntry entries [512];

		Level1PageTable level1Table[512] {};
	};

	// TODO: Move to common file
	struct __attribute__((packed, aligned(4096))) Level3PageTable {
		PageEntry entries [512];

		Level2PageTable level2Table[512] {};
	};

	// TODO: Move to common file
	struct __attribute__((packed, aligned(4096))) Level4PageTable {
		PageEntry entries [512];

		Level3PageTable level3Table[512] {};
	};

	// TODO: Move to common file
	struct __attribute__((packed, aligned(4096))) Level5PageTable {
		PageEntry entries [512];

		Level4PageTable level4Table[512] {};
	};

	class PagingManager {
	public:
		PagingManager();

		void loadPageTable();

	private:
		uPtr *paging{};
	};

	extern "C" void loadPageTableAsm(uPtr *pageTablePointer);
}

#endif