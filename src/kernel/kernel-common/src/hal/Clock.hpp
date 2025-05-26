#ifndef KERNEL_COMMON_CLOCK_HPP
#define KERNEL_COMMON_CLOCK_HPP

#include "Types.hpp"

#include "Vector.hpp"

namespace kernel::common::hal {
    using CalibratorFun = void(*)(u64 ms);

    struct Clock {
        const char *name {};
        usize priority {};
        u64 (*getNs)() {};
    };

    class Clocks {
    public:
        Clocks() = default;
        ~Clocks() = default;

        void registerClock(Clock *clock);

        Clock *getMainClock() const;

        bool stallNs(u64 ns);

        CalibratorFun getCalibrator();

        static void calibrate(u64 ms);

    private:
        void archPause();

        Clock *mainClock { nullptr };

        Clock *clocks[4] {}; // TODO: Make dynamic

        u8 currClockIndex {};
    };
}

#endif