#include "KvmClock.hpp"

#include "CommonMain.hpp"
#include "Cpu.hpp"
#include "utils/Asm.hpp"
#include "utils/CpuId.hpp"
#include "Math.hpp"

namespace kernel::x86_64::hal {
	using namespace common;
	using namespace utils;

	KvmClockInfo KvmClock::info;

	i64 KvmClock::offset;

	bool KvmClock::supported() {
		if (const u32 kvmBase = CpuId::getKvmBase(); kvmBase != 0) {
			return CpuId::get(kvmBase + 1, 0).eax & (1 << 3);
		}

		return false;
	}

	u64 KvmClock::tscFreq() {
		u64 freq = (1'000'000'000ull << 32) / info.tscToSystemMul;

		if (info.tscShift < 0) {
			freq <<= -info.tscShift;
		} else {
			freq >>= info.tscShift;
		}

		return freq;
	}

	void KvmClock::init() {
		Terminal* terminal = CommonMain::getTerminal();

		if (not this->supported()) {
			terminal->debug("Kvm clock not supported!", "KvmClock");

			return;
		}

		const u64 alignedInfoAddr = alignDown<u64>(reinterpret_cast<u64>(&info), pageSize);
		const u64 addrOffset = reinterpret_cast<u64>(&info) - alignedInfoAddr;

		const u64 infoPhysAddr = CommonMain::getInstance()->getKernelAllocContext()->pageMap.getPhysAddress(alignedInfoAddr) + addrOffset;

		Asm::wrmsr(0x4B564D01, infoPhysAddr | 1);

		if (const Clock *currClock = CommonMain::getInstance()->getClocks()->getMainClock(); currClock != nullptr) {
			offset = getNs() - currClock->getNs();
		}

		this->clock = {
			.name = "Kvm",
			.priority = 100,
			.getNs = &KvmClock::getNs,
		};

		CommonMain::getInstance()->getClocks()->registerClock(&this->clock);
	}

	// TODO: Fix this shit, it's off by too much
	u64 KvmClock::getNs() {
		u128 time = 0;

		while (true) {
			const u32 version = __atomic_load_n(&info.version, __ATOMIC_ACQUIRE);

			if (version & 1) {
				continue;
			}

			auto tmpTime = static_cast<u128>(CpuManager::getCurrentCore()->tsc.read()) - static_cast<u128>(info.tscTimestamp);

			if (info.tscShift >= 0) {
				tmpTime <<= info.tscShift;
			} else {
				tmpTime >>= -info.tscShift;
			}

			tmpTime = (tmpTime * static_cast<u128>(info.tscToSystemMul)) >> 32;
			time = tmpTime + static_cast<u128>(info.systemTime);

			__atomic_thread_fence(__ATOMIC_ACQUIRE);

			if (version == __atomic_load_n(&info.version, __ATOMIC_RELAXED)) {
				break;
			}
		}

		return time - offset;
	}
}