#include "Tsc.hpp"

#include "CommonMain.hpp"
#include "Main.hpp"
#include "utils/CpuId.hpp"
#include "Math.hpp"

#include "Cpu.hpp"

namespace kernel::x86_64::hal {
	using namespace common;
	using namespace utils;

	namespace {
		constexpr u64 minPlausibleTscFrequency = 100'000'000;
		constexpr u64 maxPlausibleTscFrequency = 10'000'000'000;

		bool plausibleTscFrequency(const u64 freq) {
			return freq >= minPlausibleTscFrequency && freq <= maxPlausibleTscFrequency;
		}

		u64 cpuidTscFrequency() {
			if (CpuId::get(0x00, 0).eax < 0x15) {
				return 0;
			}

			const CpuIdResult res = CpuId::get(0x15, 0);

			if (res.eax == 0 || res.ebx == 0 || res.ecx == 0) {
				return 0;
			}

			return static_cast<u64>((static_cast<u128>(res.ecx) * res.ebx) / res.eax);
		}

		u64 measuredTscFrequency() {
			const CalibratorFun calibrator = Clocks::getCalibrator();

			if (calibrator == nullptr) {
				return 0;
			}

			u64 freq = 0;
			constexpr u64 times = 3;

			for (u64 i = 0; i < times; i++) {
				constexpr u64 millis = 50;

				const u64 start = Tsc::read();

				calibrator(millis);

				const u64 end = Tsc::read();

				freq += (end - start) * (1'000 / millis);
			}

			return freq / times;
		}
	}

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

		if (reinterpret_cast<Kernel *>(CommonMain::getInstance())->getKvmClock()->supported()) {
			freq = reinterpret_cast<Kernel *>(CommonMain::getInstance())->getKvmClock()->tscFreq();

			if (plausibleTscFrequency(freq)) {
				this->calibrated = true;
			}
		}

		if (!this->calibrated) {
			freq = cpuidTscFrequency();

			if (plausibleTscFrequency(freq)) {
				this->calibrated = true;
			}
		}

		if (!this->calibrated) {
			freq = measuredTscFrequency();

			if (plausibleTscFrequency(freq)) {
				this->calibrated = true;
			}
		}

		if (this->calibrated) {
			auto [val1, val2] = freq2NsPN(freq);

			this->p = val1;
			this->n = val2;

			if (const Clock *mainClock = CommonMain::getInstance()->getClocks()->getMainClock()) {
				CpuManager::getCurrentCore()->offset = getTimeNs() - mainClock->getNs();
			}

			this->calibrated = true;
		}

		CommonMain::getTerminal()->debug("Timer frequency: %lu Hz", "TSC", freq);
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
			.priority = 25, // TODO: Was 75
			.getNs = &Tsc::getNs,
		};

		if (this->calibrated) {
			CommonMain::getInstance()->getClocks()->registerClock(&this->clock);
		}
	}

	Pair Tsc::getPN() const {
		return { this->p, this->n };
	}

	u64 Tsc::getNs() {
		CpuCore *currentCore = CpuManager::getCurrentCore();

		return currentCore->tsc.getTimeNs();
	}
}
