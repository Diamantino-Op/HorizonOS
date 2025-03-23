#ifndef KERNEL_X86_64_TSS_HPP
#define KERNEL_X86_64_TSS_HPP

#include "Types.hpp"

static constexpr u32 PAGE_SIZE = 4096;

namespace x86_64::hal {
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
            TssManager() = default;
            ~TssManager() = default;

            void initTss();
            void updateTss();

            Tss getTss();

        private:
            Tss tssInstance{};

            u8 kernelStack[PAGE_SIZE * 1024]; // 4MB Stack
    };

    extern "C" void updateTssAsm();
}

#endif
