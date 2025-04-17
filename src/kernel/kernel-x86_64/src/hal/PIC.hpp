#ifndef KERNEL_X86_64_PIC_HPP
#define KERNEL_X86_64_PIC_HPP

#include "Types.hpp"

namespace kernel::x86_64::hal {
    constexpr u8 pic1Address = 0x20;
    constexpr u8 pic2Address = 0xA0;

    constexpr u8 commandAddress = 0x0;
    constexpr u8 dataAddress = 0x1;

    constexpr u8 pic1Offset = 0x20;
    constexpr u8 pic2Offset = 0x28;

    constexpr u8 icw1Init = 0x20;
    constexpr u8 icw1Icw4 = 0x28;

    class PIC {
    public:
        PIC() = default;
        ~PIC() = default;

    private:
        u8 address;
    };

    class DualPIC {

    };
}

#endif