#ifndef KERNEL_X86_64_TSS_HPP
#define KERNEL_X86_64_TSS_HPP

#include "Types.hpp"

static constexpr u32 PAGE_SIZE = 4096;

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

        void updateTss();

        Tss getTss() const;

    private:
        Tss tssInstance{};

        //u8 kernelStack[PAGE_SIZE * 1]{}; // 4KB Stack
    };

    extern "C" void updateTssAsm();
}

#endif
