#ifndef KERNEL_X86_64_MAIN_HPP
#define KERNEL_X86_64_MAIN_HPP

#include "hal/GDT.hpp"
#include "hal/TSS.hpp"
#include "hal/IDT.hpp"
#include "memory/VirtualMemory.hpp"
#include "memory/X86VirtualMemory.hpp"

#include "Terminal.hpp"

namespace kernel::x86_64 {
    using namespace hal;
    using namespace memory;

    using namespace common;
    using namespace common::memory;

    class Kernel {
    public:
        Kernel();

        void halt();

        static Terminal* getTerminal();

    private:
        static Terminal terminal;

        GdtManager gdtManager;
        TssManager tssManager;
        IDTManager idtManager;
        VirtualMemoryManager<PageTable> pagingManager;
    };
}

#endif