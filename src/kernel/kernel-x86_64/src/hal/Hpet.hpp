#ifndef KERNEL_X86_64_HPET_HPP
#define KERNEL_X86_64_HPET_HPP

#include "Types.hpp"

#include "hal/Clock.hpp"

namespace kernel::x86_64::hal {
    using namespace common::hal;

    constexpr u64 regCap = 0x00;
    constexpr u64 regCfg = 0x10;
    constexpr u64 regCnt = 0xF0;

    class Hpet {
    public:
        Hpet() = default;
        ~Hpet() = default;

        void init();

        bool supported();

        u64 read(u64 offset) const;
        u64 read() const;

        void write(u64 offset, u64 val) const;

        bool isInitialized() const;

        static void calibrate(u64 ms);

        static u64 getNs();

    private:
        Clock clock {};

        bool initialized {};

        bool is64Bit {};

        u64 physAddr {};
        u64 virtAddr {};

        static u64 frequency;

        static u64 offset;

        static u64 p;
        static u64 n;

        static u64 lastReadVal;
    };
}

#endif