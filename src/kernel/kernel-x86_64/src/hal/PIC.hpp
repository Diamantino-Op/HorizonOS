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

    constexpr u8 icw1Init = 0x10;
    constexpr u8 icw1Icw4 = 0x01;

    class PIC {
    public:
		explicit PIC(u8 address);
        ~PIC() = default;

        void eoi() const;
        void disable() const;
        void cmd(u8 cmd) const;
        void dataOut(u8 data) const;
        u8 dataIn() const;

    private:
        u8 address {};
    };

    class DualPIC {
    public:
        DualPIC();
        ~DualPIC() = default;

        void init() const;
        void eoi(usize intNo) const;
        void disable() const;

        void mask(u8 id) const;
        void unmask(u8 id) const;

    private:
        PIC pic1;
        PIC pic2;
    };
}

#endif