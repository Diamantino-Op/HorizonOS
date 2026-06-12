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

	auto Clocks::getMainClock() const -> Clock * {
		return mainClock;
	}

	auto Clocks::stallNs(const u64 ns) const -> bool {
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

	void CoreClock::resetSchedulerTimer() {
		const Clocks *clocksPtr = CommonMain::getInstance()->getClocks();

		this->schedulerHandler.nextCall = clocksPtr->getMainClock()->getNs() + this->schedulerHandler.timeout;
	}

	void CoreClock::addTimerHandle(const HandlerFun fun, const u64 timeout) {
		auto *newHandler = new TimerHandler();
		
		newHandler->fun = fun;
		newHandler->timeout = timeout;

		this->handlers.addEnd(newHandler);
	}
}