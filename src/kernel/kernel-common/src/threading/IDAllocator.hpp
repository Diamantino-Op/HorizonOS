#ifndef KERNEL_COMMON_IDALLOCATOR_HPP
#define KERNEL_COMMON_IDALLOCATOR_HPP

#include "Types.hpp"

namespace kernel::common::threading {
    constexpr u16 maxProcesses = 32768;
    constexpr u16 maxThreads = 65535;

    class PIDAllocator {
    public:
        static void init();
        static u16 allocPID();
        static void freePID(u16 pid);

    private:
        static u16 freePIDs[maxProcesses];
        static i32 pidTop;
    };

    class TIDAllocator {
    public:
        static void init();
        static u16 allocTID();
        static void freeTID(u16 tid);

    private:
        static u16 freeTIDs[maxThreads];
        static i32 tidTop;
    };
}

#endif