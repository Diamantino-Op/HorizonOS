#ifndef KERNEL_X86_64_SYSCALLX86_HPP
#define KERNEL_X86_64_SYSCALLX86_HPP

#include "Types.hpp"
#include "Interrupts.hpp"
#include "hal/Syscall.hpp"

namespace kernel::x86_64::hal {
    constexpr u64 signalSigreturnSyscall = 19;

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
	};

	void intSyscallEntry(Frame *frame);
	void deliverPendingSignal(Frame *frame);
	void deliverPendingSignal(SyscallRegs *regs);

    extern "C" void callSyscall(SyscallRegs *regs);
}

#endif
