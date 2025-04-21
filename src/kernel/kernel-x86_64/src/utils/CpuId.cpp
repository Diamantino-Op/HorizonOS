#include "CpuId.hpp"

#include "CommonMain.hpp"

namespace kernel::x86_64::utils {
	using namespace kernel::common;

	CpuIdRegs CpuId::getCpuIdRegs(u32 leaf, u32 subLeaf) {
		Terminal* terminal = CommonMain::getTerminal();

		u32 maxLeaf = 0;

		asm volatile("cpuid"
					 : "=a"(maxLeaf)
					 : "a"(leaf & 0x80000000)
					 : "rbx", "rcx", "rdx");

		if (leaf > maxLeaf) {
			terminal->error("Leaf out of range!", "CpuId");
		}

		CpuIdRegs result {};

		asm volatile("cpuid" : "=a"(result.eax), "=b"(result.ebx), "=c"(result.ecx), "=d"(result.edx) : "a"(leaf), "c"(subLeaf));

		return result;
	}
}