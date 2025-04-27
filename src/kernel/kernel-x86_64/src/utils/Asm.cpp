#include "Asm.hpp"

namespace kernel::x86_64::utils {
	void Asm::cli() {
		asm volatile("cli");
	}

	void Asm::sti() {
		asm volatile("sti");
	}

	void Asm::hlt() {
		asm volatile("hlt");
	}

	[[noreturn]] void Asm::lhlt() {
		for (;;) {
			hlt();
		}
	}

	void Asm::pause() {
		asm volatile("pause");
	}


	void Asm::invalidatePage(u64 addr) {
		asm volatile("invlpg (%0)" :: "r" (addr) : "memory");
	}

	// CRs

	u64 Asm::readCr0() {
		u64 cr0Val = 0;

		asm volatile("mov %%cr0, %0" : "=r"(cr0Val));

		return cr0Val;
	}

	void Asm::writeCr0(u64 value) {
		asm volatile("mov %0, %%cr0" :: "a"(value));
	}

	u64 Asm::readCr2() {
		u64 cr2Val = 0;

		asm volatile("mov %%cr2, %0" : "=r"(cr2Val));

		return cr2Val;
	}

	u64 Asm::readCr3() {
		u64 cr3Val = 0;

		asm volatile("mov %%cr3, %0" : "=r"(cr3Val));

		return cr3Val;
	}

	void Asm::writeCr3(u64 value) {
		asm volatile("mov %0, %%cr3" :: "a"(value));
	}

	u64 Asm::readCr4() {
		u64 cr4Val = 0;

		asm volatile("mov %%cr4, %0" : "=r"(cr4Val));

		return cr4Val;
	}

	void Asm::writeCr4(u64 value) {
		asm volatile("mov %0, %%cr4" :: "a"(value));
	}

	u64 Asm::readCr8() {
		u64 cr8Val = 0;

		asm volatile("mov %%cr8, %0" : "=r"(cr8Val));

		return cr8Val;
	}

	void Asm::writeCr8(u64 value) {
		asm volatile("mov %0, %%cr8" :: "a"(value));
	}

	// AVX / SSE

	u64 Asm::readXCr(u32 i) {
		u32 eax, edx;

		asm volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(i) : "memory");

		return eax | (static_cast<u64>(edx) << 32);
	}

	void Asm::writeXCr(u32 i, u64 value) {
		u32 edx = value >> 32;
		u32 eax = static_cast<u32>(value);

		asm volatile("xsetbv" :: "a"(eax), "d"(edx), "c"(i) : "memory");
	}

	void Asm::xsave(const u64 *region) {
		asm volatile("xsave (%0)" :: "a"(region));
	}

	void Asm::xrstor(const u64 *region) {
		asm volatile("xrstor (%0)" ::"a"(region));
	}

	void Asm::fninit() {
		asm volatile("fninit");
	}

	void Asm::fxsave(const u64 *region) {
		asm volatile("fxsave (%0)" :: "a"(region));
	}

	void Asm::fxrstor(const u64 *region) {
		asm volatile("fxrstor (%0)" ::"a"(region));
	}

	// Msrs

	u64 Asm::rdmsr(u64 msr) {
		u32 low, high;

		asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(static_cast<u64>(msr)));

		return (static_cast<u64>(high) << 32) | low;
	}

	void Asm::wrmsr(u64 msr, u64 value) {
		u32 low = value & 0xFFFFFFFF;
		u32 high = value >> 32;

		asm volatile("wrmsr" :: "c"(static_cast<u64>(msr)), "a"(low), "d"(high));
	}

}