#ifndef KERNEL_X86_64_KVMCLOCK_HPP
#define KERNEL_X86_64_KVMCLOCK_HPP

#include "Types.hpp"

#include "hal/Clock.hpp"

namespace kernel::x86_64::hal {
    using namespace common::hal;

    struct __attribute__((packed)) KvmClockInfo {
        u32 version {};
        u32 pad0 {};
        u64 tscTimestamp {};
        u64 systemTime {};
        u32 tscToSystemMul {};
        i8 tscShift {};
        u8 flags {};
        u8 pad[2];
    };

    class KvmClock {
    public:
        KvmClock() = default;
        ~KvmClock() = default;

        bool supported();

        u64 tscFreq();

        void init();

        static u64 getNs();

    private:
        static KvmClockInfo info;

        static u64 offset;

		Clock clock;
    };
}

#endif