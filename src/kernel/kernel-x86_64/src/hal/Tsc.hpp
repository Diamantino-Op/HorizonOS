#ifndef KERNEL_X86_64_TSC_HPP
#define KERNEL_X86_64_TSC_HPP

#include "Types.hpp"
#include "hal/Clock.hpp"

namespace kernel::x86_64::hal {
    using namespace common::hal;

    class Tsc {
    public:
        Tsc() = default;
        ~Tsc() = default;

        bool supported();

        u64 read();
        u64 getTimeNs();

        void calibrate();

        void init();

        void globalInit();

        static u64 getNs();

    private:
        Clock clock {};

        u64 p {};
        u64 n {};
        bool calibrated {};
    };
}

#endif