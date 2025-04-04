#ifndef KERNEL_X86_64_PAGING_HPP
#define KERNEL_X86_64_PAGING_HPP

#include "Types.hpp"

namespace kernel::x86_64::memory {
	enum PageTableFlags : u8 {
		PAGE_PRESENT = 0b00000001,
		PAGE_READ_WRITE = 0b00000010,
		PAGE_USER = 0b00000100,
		PAGE_WRITE_TROUGH = 0b00001000,
		PAGE_CACHE_DISABLE = 0b00010000,
		PAGE_ACCESSED = 0b00100000,
		PAGE_SIZE = 0b10000000
	};

    struct __attribute__((packed)) PageEntry {
    	u8 flags{};
    	u8 availableLow : 4 {};
    	u64 address : 40 {};
		u32 availableHigh : 11 {};
    	u8 executeDisable : 1 {};
    };

	struct __attribute__((packed)) PageDirectoryTable {
		PageEntry __attribute__((aligned(4096))) table[1024]{};
	};

	struct __attribute__((packed)) PageDirectoryPointerTable {
		PageEntry __attribute__((aligned(32))) table[4]{};
	};

	class PagingManager {
	public:
		PagingManager();

	private:
		PageDirectoryTable pageDirectoryTables[4]{}; // TODO: Make Dynamic
		PageDirectoryPointerTable pageDirectoryPointerTable{}; // TODO: Make Dynamic
	};

	extern "C" void initPagingAsm(u64 pageTablePointer);
}

#endif