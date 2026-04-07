#include "SyscallX86.hpp"

#include "CommonMain.hpp"
#include "GDT.hpp"
#include "utils/Asm.hpp"

namespace kernel::common::hal {
	using namespace x86_64::hal;
	using namespace x86_64::utils;

	void SyscallManager::initArch() {
		constexpr u64 star = static_cast<u64>(Selector::USER_CODE32) << 48 | static_cast<u64>(Selector::KERNEL_CODE) << 32;

		Asm::wrmsr(Msrs::STAR, star);
		Asm::wrmsr(Msrs::LSTAR, reinterpret_cast<u64>(syscallHandler));
		Asm::wrmsr(Msrs::FMASK, 0x200 | 0x400);

		u64 efer = Asm::rdmsr(Msrs::EFER);

		efer |= (1 << 0);
		// efer |= (1 << 12); // SVME
		// efer |= (1 << 15); // TCE

		Asm::wrmsr(Msrs::EFER, efer);
	}

	void SyscallManager::setGsBase(const u64 gsBase) {
		Asm::wrmsr(Msrs::KGSBAS, gsBase);
	}

	void SyscallManager::setFsBase(const u64 fsBase) {
		Asm::wrmsr(Msrs::FSBAS, fsBase);
	}

	u64 SyscallManager::getGsBase() {
		return Asm::rdmsr(Msrs::KGSBAS);
	}

	u64 SyscallManager::getFsBase() {
		return Asm::rdmsr(Msrs::FSBAS);
	}

	u64 SyscallManager::syscallGetTID(long *ret, u64, u64, u64, u64, u64, u64) {
		*ret = Scheduler::getCurrentThread()->getId();

		return 0;
	}
}

namespace kernel::x86_64::hal {
	using namespace common;

	void intSyscallEntry(Frame *frame) {
		CommonMain::getTerminal()->debug("Syscall: %lu", "Syscalls", frame->rax);

		long ret = 0;

		// TODO: Check the compat OS
		frame->rdx = SyscallManager::horizonSyscalls[frame->rax](&ret, frame->rdi, frame->rsi, frame->rdx, frame->r10, frame->r8, frame->r9);

		frame->rax = ret;
	}

	void callSyscall(SyscallRegs *regs) {
		CommonMain::getTerminal()->debug("Syscall: %lu", "Syscalls", regs->rax);

		long ret = 0;

		// TODO: Check the compat OS
		regs->rdx = SyscallManager::horizonSyscalls[regs->rax](&ret, regs->rdi, regs->rsi, regs->rdx, regs->r10, regs->r8, regs->r9);

		regs->rax = ret;
	}
}