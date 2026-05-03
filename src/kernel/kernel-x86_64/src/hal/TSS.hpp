#ifndef KERNEL_X86_64_TSS_HPP
#define KERNEL_X86_64_TSS_HPP

#include "Types.hpp"
#include "memory/MainMemory.hpp"
#include <cstddef>

namespace kernel::x86_64::hal {
	using namespace kernel::common::memory;

	constexpr u16 iopbSize = 8192; // 65536 ports / 8 bits

	struct TssPtrs {
		u64 rsp[3];
		u64 ist[7];
	};

	struct __attribute__((packed, aligned(4))) TssTemp {
		u32 _reserved{};
		u64 rsp[3];
		u64 _reserved1{};
		u64 ist[7];
		u64 _reserved2{};
		u16 _reserved3{};
		u16 iopbOffset{};

		constexpr TssTemp() {}
	};

    struct __attribute__((packed, aligned(4))) Tss {
        u32 _reserved{};
        u64 rsp[3];
        u64 _reserved1{};
        u64 ist[7];
        u64 _reserved2{};
        u16 _reserved3{};
        u16 iopbOffset{};

    	u8 iopb[iopbSize];
    	u8 iopbTerminator{0xFF};

    	constexpr Tss() {
    		memset(iopb, 0xFF, sizeof(iopb));

    		iopbOffset = offsetof(Tss, iopb);
    	}
    };

    class TssManager {
    public:
        void allocStack();

        void updateTss();

    	TssTemp *getTssTemp();

    	TssPtrs *getTssPtrs();

    private:
    	TssTemp tssTemp {};

    	TssPtrs tssPtrs {};

        const u8 generalIntStack[2048] = {};
        const u8 nmiIntStack[2048] = {};
        const u8 exceptionIntStack[2048] = {};
    	const u8 syscallStack[2048] = {};
    };

    extern "C" void updateTssAsm();
}

#endif
