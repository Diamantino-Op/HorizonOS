#ifndef KERNEL_COMMON_CLOCK_HPP
#define KERNEL_COMMON_CLOCK_HPP

#include "LinkedList.hpp"
#include "Types.hpp"

namespace kernel::common::hal {
    using CalibratorFun = void (*)(u64 ms);

    using GetNsFun = u64 (*)();

	using HandlerFun = void (*)();

    struct Clock {
        const char *name {};
        usize priority {};
        GetNsFun getNs {};
    };

	struct TimerHandler {
		HandlerFun fun {};
		u64 nextCall {};
		u64 timeout {};
	};

    class Clocks {
    public:
        Clocks() = default;
        ~Clocks() = default;

        void registerClock(Clock *clock);

        Clock *getMainClock() const;

        bool stallNs(u64 ns);

        CalibratorFun getCalibrator();

    private:
    	static void finishTimerTick();

        static void calibrate(u64 ms);

        void archPause();

        Clock *mainClock { nullptr };

        Clock *clocks[5] {}; // TODO: Make dynamic

        u8 currClockIndex {};

    public:
    	static void addTimerHandle(HandlerFun fun, u64 timeout);

    	static u32 timerTick(u64 *);

    	static void resetSchedulerTimer();

    	LinkedList<TimerHandler> handlers {};
    	TimerHandler schedulerHandler {};
    };
}

#endif