#include "Clock.hpp"

#include "CommonMain.hpp"

namespace kernel::common::hal {
	void Clocks::registerClock(Clock *clock) {
		Terminal *terminal = CommonMain::getTerminal();

		terminal->info("Registering new clock: %s", "Clock", clock->name);

		this->clocks[this->currClockIndex] = clock;

		this->currClockIndex++;

		if (this->mainClock == nullptr) {
			this->mainClock = clock;
		} else {
			for (const auto &tmpClock : this->clocks) {
				if (tmpClock->priority > this->mainClock->priority) {
					this->mainClock = tmpClock;
				}
			}
		}

		terminal->info("New main clock: %s", "Clock", this->mainClock->name);
	}

	Clock *Clocks::getMainClock() const {
		return mainClock;
	}

	bool Clocks::stallNs(const u64 ns) {
		if (mainClock == nullptr) {
			return false;
		}

		const u64 target = mainClock->getNs() + ns;

		while (mainClock->getNs() < target) {
			archPause();
		}

		return true;
	}

	CalibratorFun Clocks::getCalibrator() {

		return calibrate;
	}

	void Clocks::calibrate(u64 ms) {

	}
}