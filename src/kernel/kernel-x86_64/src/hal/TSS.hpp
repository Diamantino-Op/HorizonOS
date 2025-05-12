#ifndef KERNEL_X86_64_TSS_HPP
#define KERNEL_X86_64_TSS_HPP

#include "Types.hpp"

namespace kernel::x86_64::hal {
    struct __attribute__((packed)) Tss {
        u32 _reserved{};
        u64 rsp[3];
        u64 _reserved1{};
        u64 ist[7];
        u64 _reserved2{};
        u16 _reserved3{};
        u16 iopbOffset{};

        constexpr Tss() = default;
    };

    class TssManager {
    public:
        TssManager();

        void allocStack();

        void updateTss();

        Tss getTss() const;

    private:
        Tss tssInstance{};
    };

    extern "C" void updateTssAsm();
}

#endif
