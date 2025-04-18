#ifndef KERNEL_X86_64_MAIN_HPP
#define KERNEL_X86_64_MAIN_HPP

#include "CommonMain.hpp"

#include "hal/GDT.hpp"
#include "hal/IDT.hpp"
#include "hal/TSS.hpp"
#include "hal/PIC.hpp"
#include "memory/X86VirtualMemory.hpp"

namespace kernel::x86_64 {
    using namespace hal;
    using namespace memory;

    using namespace common;
    using namespace common::memory;

    class Kernel : CommonMain {
    public:
        Kernel();

        void init() override;

        GdtManager *getGdtManager();
        TssManager *getTssManager();
        IdtManager *getIdtManager();
        PhysicalMemoryManager *getPMM();
        VirtualMemoryManager *getVMM();

    protected:
        void halt() override;

    private:
        GdtManager gdtManager;
        TssManager tssManager;
        IdtManager idtManager;

        DualPIC dualPic;

        PhysicalMemoryManager physicalMemoryManager;
        VirtualMemoryManager virtualMemoryManager;
    };
}

#endif