#ifndef KERNEL_X86_64_SYSCALLX86_HPP
#define KERNEL_X86_64_SYSCALLX86_HPP

#include "Types.hpp"
#include "hal/Syscall.hpp"

namespace kernel::x86_64::hal {
    struct SyscallRegs {
    	u64 rax = {};
    	u64 rbx = {};
    	u64 rcx = {};
    	u64 rdx = {};
    	u64 rsi = {};
    	u64 rdi = {};
    	u64 r8 = {};
    	u64 r9 = {};
    	u64 r10 = {};
    	u64 r11 = {};
    	u64 r12 = {};
    	u64 r13 = {};
    	u64 r14 = {};
    	u64 r15 = {};

    	constexpr u64 *raxReg() {
    	    return &rax;
    	}

    	constexpr u64 *rbxReg() {
    	    return &rax;
    	}

		constexpr u64 *rcxReg() {
			return &rax;
		}

		constexpr u64 *rdxReg() {
			return &rax;
		}

		constexpr u64 *rsiReg() {
			return &rax;
		}

		constexpr u64 *rdiReg() {
			return &rax;
		}

		constexpr u64 *r8Reg() {
			return &rax;
		}

		constexpr u64 *r9Reg() {
			return &rax;
		}

		constexpr u64 *r10Reg() {
			return &rax;
		}

		constexpr u64 *r11Reg() {
			return &rax;
		}

		constexpr u64 *r12Reg() {
			return &rax;
		}

		constexpr u64 *r13Reg() {
			return &rax;
		}

		constexpr u64 *r14Reg() {
			return &rax;
		}

		constexpr u64 *r15Reg() {
			return &rax;
		}
	};

	static u32 intSyscallEntry(u64 *regs);

    void callSyscall(SyscallRegs *regs);
}

#endif
