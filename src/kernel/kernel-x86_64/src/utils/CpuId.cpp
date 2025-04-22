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
		static char vendor[12] {};

		const CpuIdResult res = get(0x00, 0x00);

		reinterpret_cast<u32 *>(vendor)[0] = res.ebx;
		reinterpret_cast<u32 *>(vendor)[1] = res.edx;
		reinterpret_cast<u32 *>(vendor)[2] = res.ecx;

		return vendor;
	}

	char *CpuId::getBrand() {
		static char brand[48];

		const CpuIdResult res1 = get(0x80000002, 0x00);
		const CpuIdResult res2 = get(0x80000003, 0x00);
		const CpuIdResult res3 = get(0x80000004, 0x00);

		reinterpret_cast<u32 *>(brand)[0] = res1.eax;
		reinterpret_cast<u32 *>(brand)[1] = res1.ebx;
		reinterpret_cast<u32 *>(brand)[2] = res1.ecx;
		reinterpret_cast<u32 *>(brand)[3] = res1.edx;

		reinterpret_cast<u32 *>(brand)[4] = res2.eax;
		reinterpret_cast<u32 *>(brand)[5] = res2.ebx;
		reinterpret_cast<u32 *>(brand)[6] = res2.ecx;
		reinterpret_cast<u32 *>(brand)[7] = res2.edx;

		reinterpret_cast<u32 *>(brand)[8] = res3.eax;
		reinterpret_cast<u32 *>(brand)[9] = res3.ebx;
		reinterpret_cast<u32 *>(brand)[10] = res3.ecx;
		reinterpret_cast<u32 *>(brand)[11] = res3.edx;

		return brand;
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
		CpuIdResult res = get(0x07, 0x00);

		return res.ebx & (1 << 16);
	}

	u32 CpuId::getXSaveSize() {
		if (!hasXSave()) {
			return 0;
		}

		const CpuIdResult res = get(0x0d, 0x00);

		return res.ecx;
	}
}