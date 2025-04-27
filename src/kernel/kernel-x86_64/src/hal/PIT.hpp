#ifndef KERNEL_X86_64_PIT_HPP
#define KERNEL_X86_64_PIT_HPP

#include "Types.hpp"

namespace kernel::x86_64::hal {
    constexpr u16 commandModePortAddress = 0x43;
    constexpr u16 channel0DataAddress = 0x40;
    constexpr u16 channel1DataAddress = 0x41;
    constexpr u16 channel2DataAddress = 0x42;

    constexpr u64 frequency = 1193180;

    enum CMChannels : u8 {
        CHANNEL0 = 0b00000000,
        CHANNEL1 = 0b01000000,
        CHANNEL2 = 0b10000000,
        READ_BACK = 0b11000000,
    };

    enum CMAccess : u8 {
        LATCH_COUNT_CMD = 0b00000000,
        LOBYTE = 0b00010000,
        HIBYTE = 0b00100000,
        HILOBYTE = 0b00110000,
    };

    enum CMModes : u8 {
        INTERRUPT_TERMINAL = 0b00000000,
        HW_RETRIG_ONESHOT = 0b00000010,
        RATE_GEN = 0b00000100,
        SQUARE_WAVE = 0b00000110,
        SW_STROBE = 0b00001000,
        HW_STROBE = 0b00001010,
        RATE_GEN2 = 0b00001100,
        SQUARE_WAVE2 = 0b00001110,
    };

    enum CMBinModes : u8 {
        BINARY_16BIT = 0b00000000,
        BCD_4DIG = 0b00000001,
    };

    class PIT {
    public:
        PIT() = default;
        ~PIT() = default;

        void init(isize freq);
        u16 readCount();

    private:
        usize ticks;
    };
}

#endif