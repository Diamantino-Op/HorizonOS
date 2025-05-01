#ifndef KERNEL_X86_64_IOPORT_HPP
#define KERNEL_X86_64_IOPORT_HPP

#include "Types.hpp"

namespace kernel::x86_64::hal {
    class IOPort {
    public:
        static void out8(u8 data, u16 address);
        static void out16(u16 data, u16 address);
        static void out32(u32 data, u16 address);
        static void out64(u64 data, u16 address);

        static u8 in8(u16 address);
        static u16 in16(u16 address);
        static u32 in32(u16 address);
        static u64 in64(u16 address);
    };
}

#endif