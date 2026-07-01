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
		CpuCore *currentCore = CpuManager::getCurrentCore();
		auto *main = CommonMain::getInstance();
		const Clocks *clocksPtr = main != nullptr ? main->getClocks() : nullptr;
		const Clock *mainClock = clocksPtr != nullptr ? clocksPtr->getMainClock() : nullptr;

		if (currentCore == nullptr || mainClock == nullptr || mainClock->getNs == nullptr) {
			if (currentCore != nullptr && currentCore->apic.isInitialized()) {
				currentCore->apic.eoi();
			}

			return 1;
		}

		u64 now = mainClock->getNs();

		auto it = currentCore->coreClock.handlers.begin();
		const auto end = currentCore->coreClock.handlers.end();

		while (it != end) {
			auto &currEntry = *it;
			auto nextIt = it;
			++nextIt;

			if (currEntry.fun != nullptr && currEntry.nextCall <= now) {
				currEntry.nextCall = now + currEntry.timeout;

				currEntry.fun();
				now = mainClock->getNs();
			}

			it = nextIt;
		}

		TimerHandler &schedulerHandler = currentCore->coreClock.schedulerHandler;

		if (schedulerHandler.fun != nullptr && schedulerHandler.nextCall <= now) {
			schedulerHandler.nextCall = now + schedulerHandler.timeout;

			schedulerHandler.fun();
		} else {
			finishTimerTick();
		}

		return 1;
	}
}
