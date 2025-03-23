#ifndef KERNEL_X86_64_MAIN_HPP
#define KERNEL_X86_64_MAIN_HPP

#include "hal/TSS.hpp"
#include "hal/GDT.hpp"

namespace x86_64 {
    using namespace hal;

    class Kernel {
    public:
        Kernel() = default;
        ~Kernel() = default;

        void init();
        void halt();

    private:
        GdtManager gdtManager;
        TssManager tssManager;
    };
}

#endif