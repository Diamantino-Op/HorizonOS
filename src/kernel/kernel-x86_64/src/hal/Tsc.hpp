#ifndef KERNEL_X86_64_TSC_HPP
#define KERNEL_X86_64_TSC_HPP

#include "Types.hpp"

namespace kernel::x86_64::hal {
    struct CpuCore;

    class Tsc {
    public:
        Tsc() = default;
        ~Tsc() = default;

        bool supported();

        u64 read();
        u64 getTimeNs();

        void calibrate();

        void init();
        void finalise();

        void setCore(CpuCore *core);
        CpuCore *getCore() const;

    public:
        CpuCore *core {};

        u64 p {};
        u64 n {};
        bool calibrated {};
    };
}

#endif