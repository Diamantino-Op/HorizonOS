#include "Tsc.hpp"

#include "CommonMain.hpp"
#include "Main.hpp"
#include "utils/CpuId.hpp"
#include "Math.hpp"

#include "Cpu.hpp"

namespace kernel::x86_64::hal {
	using namespace common;
	using namespace utils;

	bool Tsc::supported() {
		return CpuId::get(0x80000007, 0).edx & (1 << 8);
	}

	u64 Tsc::read() {
		u32 a = 0;
		u32 d = 0;

		asm volatile ("lfence; rdtsc" : "=a"(a), "=d"(d));

		return static_cast<u64>(a) | (static_cast<u64>(d) << 32);
	}

	u64 Tsc::getTimeNs() {
		if (!this->calibrated) {
			return 0;
		}

		return ticks2ns(read(), this->p, this->n) - CpuManager::getCurrentCore()->offset;
	}

	void Tsc::calibrate() {
		u64 freq = 0;

		if (const CpuIdResult res = CpuId::get(0x15, 0); res.eax != 0 and res.ebx != 0 and res.ecx != 0) {
			freq = res.ecx * res.ebx / res.eax;

			this->calibrated = true;
		} else if (reinterpret_cast<Kernel *>(CommonMain::getInstance())->getKvmClock()->supported()) {
			freq = reinterpret_cast<Kernel *>(CommonMain::getInstance())->getKvmClock()->tscFreq();

			this->calibrated = true;
		} else if (const CalibratorFun calibrator = CommonMain::getInstance()->getClocks()->getCalibrator(); calibrator != nullptr) {
			constexpr u64 times = 3;

			for (u64 i = 0; i < times; i++) {
				constexpr u64 millis = 50;

				const u64 start = this->read();

				calibrator(millis);

				const u64 end = this->read();

				freq += (end - start) * (1'000 / millis);
			}

			freq /= times;

			this->calibrated = true;
		}

		if (this->calibrated) {
			auto [val1, val2] = freq2NsPN(freq);

			this->p = val1;
			this->n = val2;

			if (const Clock *clock = CommonMain::getInstance()->getClocks()->getMainClock()) {
				CpuManager::getCurrentCore()->offset = getTimeNs() - clock->getNs();
			}

			this->calibrated = true;
		}
	}

	void Tsc::init() {
		Terminal* terminal = CommonMain::getTerminal();

		if (!supported()) {
			terminal->debug("TSC not invariant!", "TSC");

			return;
		}

		this->calibrate();

		if (this->calibrated) {
			terminal->debug("TSC calibrated!", "TSC");
		} else {
			terminal->debug("TSC not calibrated!", "TSC");
		}
	}

	void Tsc::globalInit() {
		this->clock = {
			.name = "TSC",
			.priority = 75,
			.getNs = &Tsc::getNs,
		};

		if (this->calibrated) {
			CommonMain::getInstance()->getClocks()->registerClock(&this->clock);
		}
	}

	u64 Tsc::getNs() {
		CpuCore *currentCore = CpuManager::getCurrentCore();

		return currentCore->tsc.getTimeNs();
	}
}