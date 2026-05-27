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
				if (tmpClock != nullptr and tmpClock->priority > this->mainClock->priority) {
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

	void Clocks::calibrate(const u64 ms) {
		const Clock *mainClock = CommonMain::getInstance()->getClocks()->getMainClock();

		const u64 end = mainClock->getNs() + (ms * 1'000'000);

		while (mainClock->getNs() < end) {}
	}

	u32 Clocks::timerTick(u64 *) {
		Clocks *clocksPtr = CommonMain::getInstance()->getClocks();

		auto it = clocksPtr->handlers.begin();
		const auto end = clocksPtr->handlers.end();

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

		if (clocksPtr->schedulerHandler.nextCall <= clocksPtr->getMainClock()->getNs()) {
			clocksPtr->schedulerHandler.nextCall = clocksPtr->getMainClock()->getNs() + clocksPtr->schedulerHandler.timeout;

			clocksPtr->schedulerHandler.fun();
		} else {
			finishTimerTick();
		}

		return 1;
	}

	void Clocks::resetSchedulerTimer() {
		Clocks *clocksPtr = CommonMain::getInstance()->getClocks();

		clocksPtr->schedulerHandler.nextCall = clocksPtr->getMainClock()->getNs() + clocksPtr->schedulerHandler.timeout;
	}

	void Clocks::addTimerHandle(const HandlerFun fun, const u64 timeout) {
		Clocks *clocksPtr = CommonMain::getInstance()->getClocks();

		auto *newHandler = new TimerHandler();
		
		newHandler->fun = fun;
		newHandler->timeout = timeout;

		clocksPtr->handlers.addEnd(newHandler);
	}
}