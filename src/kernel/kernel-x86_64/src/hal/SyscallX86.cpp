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
		Asm::wrmsr(Msrs::FMASK, ~0x2);
	}
}