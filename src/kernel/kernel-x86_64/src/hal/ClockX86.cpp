#include "Interrupts.hpp"
#include "hal/Clock.hpp"

#include "utils/Asm.hpp"
#include "Main.hpp"

namespace kernel::common::hal {
	using namespace x86_64;
	using namespace x86_64::utils;

	auto Clocks::getCalibrator() -> CalibratorFun {
		auto *kernel = reinterpret_cast<Kernel *>(CommonMain::getInstance());

		if (kernel->getKvmClock()->supported()) {
			return calibrate;
		}

		if (kernel->getHpet()->isInitialized()) {
			return Hpet::calibrate;
		}

		if (CommonMain::getInstance()->getAcpiPM()->supported()) {
			return AcpiPM::calibrate;
		}

		return calibrate;
	}

	void Clocks::archPause() {
		Asm::pause();
	}

	void Clocks::finishTimerTick() {
		Interrupts::sendEOI();
	}

	auto Clocks::timerTick(u64 */* unused */) -> u32 {
		const Clocks *clocksPtr = CommonMain::getInstance()->getClocks();
		CpuCore *currentCore = CpuManager::getCurrentCore();

		auto it = currentCore->coreClock.handlers.begin();
		const auto end = currentCore->coreClock.handlers.end();

		while (it != end) {
			auto &currEntry = *it;
			auto nextIt = it;
			++nextIt;

			if (currEntry.nextCall <= clocksPtr->getMainClock()->getNs()) {
				currEntry.nextCall = clocksPtr->getMainClock()->getNs() + currEntry.timeout;

				currEntry.fun();
			}

			it = nextIt;
		}

		if (currentCore->coreClock.schedulerHandler.nextCall <= clocksPtr->getMainClock()->getNs()) {
			currentCore->coreClock.schedulerHandler.nextCall = clocksPtr->getMainClock()->getNs() + currentCore->coreClock.schedulerHandler.timeout;

			currentCore->coreClock.schedulerHandler.fun();
		} else {
			finishTimerTick();
		}

		return 1;
	}
}