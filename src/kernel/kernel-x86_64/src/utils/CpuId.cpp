#include "CpuId.hpp"

#include "CommonMain.hpp"

namespace kernel::x86_64::utils {
	using namespace kernel::common;

	CpuIdResult CpuId::get(u32 leaf, u32 subLeaf) {
		CpuIdResult result {};

		asm volatile("cpuid" : "=a" (result.eax), "=b" (result.ebx), "=c" (result.ecx), "=d" (result.edx) : "a" (leaf), "c" (subLeaf));

		return result;
	}

	char *CpuId::getVendor() {
		static Vendor vendor {};

		const CpuIdResult res = get(0x00, 0x00);

		vendor.regs[0] = res.ebx;
		vendor.regs[1] = res.edx;
		vendor.regs[2] = res.ecx;

		return vendor.value;
	}

	char *CpuId::getBrand() {
		static Brand brand {};

		const CpuIdResult res1 = get(0x80000002, 0x00);
		const CpuIdResult res2 = get(0x80000003, 0x00);
		const CpuIdResult res3 = get(0x80000004, 0x00);

		brand.regs[0] = res1.eax;
		brand.regs[1] = res1.ebx;
		brand.regs[2] = res1.ecx;
		brand.regs[3] = res1.edx;

		brand.regs[4] = res2.eax;
		brand.regs[5] = res2.ebx;
		brand.regs[6] = res2.ecx;
		brand.regs[7] = res2.edx;

		brand.regs[8] = res3.eax;
		brand.regs[9] = res3.ebx;
		brand.regs[10] = res3.ecx;
		brand.regs[11] = res3.edx;

		return brand.value;
	}

	bool CpuId::hasXSave() {
		const CpuIdResult res = get(0x01, 0x00);

		return res.ecx & (1 << 26);
	}

	bool CpuId::hasAvx() {
		const CpuIdResult res = get(0x01, 0x00);

		return res.ecx & (1 << 28);
	}

	bool CpuId::hasAvx512() {
		const CpuIdResult res = get(0x07, 0x00);

		return res.ebx & (1 << 16);
	}

	u32 CpuId::getXSaveSize() {
		if (!hasXSave()) {
			return 512;
		}

		const CpuIdResult res = get(0x0d, 0x00);

		return res.ecx;
	}
}