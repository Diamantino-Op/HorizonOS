#include "SyscallX86.hpp"

#include "GDT.hpp"
#include "utils/Asm.hpp"

namespace kernel::common::hal {
	using namespace x86_64::hal;
	using namespace x86_64::utils;

	void SyscallManager::init() {
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
}

namespace kernel::x86_64::hal {
	void intSyscallEntry(const Frame &frame) {

	}

	void callSyscall(SyscallRegs *regs) {

	}
}