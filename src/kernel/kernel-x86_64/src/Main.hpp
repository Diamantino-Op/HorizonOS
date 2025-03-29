#ifndef KERNEL_X86_64_MAIN_HPP
#define KERNEL_X86_64_MAIN_HPP

#include "hal/GDT.hpp"
#include "hal/TSS.hpp"
#include "hal/IDT.hpp"

#include "printf.h"

namespace x86_64 {
    using namespace hal;

    class Kernel {
    public:
        Kernel();

        void halt();

    private:
        GdtManager gdtManager;
        TssManager tssManager;
        IDTManager idtManager;
    };
}

#endif