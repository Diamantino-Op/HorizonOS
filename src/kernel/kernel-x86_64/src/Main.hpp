#ifndef KERNEL_X86_64_MAIN_HPP
#define KERNEL_X86_64_MAIN_HPP

#include "CommonMain.hpp"

#include "hal/GDT.hpp"
#include "hal/IDT.hpp"
#include "hal/PIC.hpp"
#include "hal/PIT.hpp"
#include "hal/TSS.hpp"
#include "hal/Cpu.hpp"
#include "memory/X86VirtualMemory.hpp"

namespace kernel::x86_64 {
    using namespace hal;
    using namespace memory;

    using namespace common;
    using namespace common::memory;

    struct CoreArgs : CommonCoreArgs {
        GdtManager *coreGdtManager {};
        TssManager *coreTssManager {};
        IdtManager *coreIdtManager {};

        PIT *corePIT {};

        CpuCore *cpuCore {};
    };

    class Kernel final : CommonMain {
    public:
        Kernel();
        ~Kernel() override = default;

        void init() override;

        GdtManager *getGdtManager();
        TssManager *getTssManager();
        IdtManager *getIdtManager();

    protected:
        void halt() override;

    private:
        GdtManager gdtManager;
        TssManager tssManager;
        IdtManager idtManager;

        DualPIC dualPic;

        PIT pit;

        CpuManager cpuManager;
    };

    class CoreKernel final : CommonCoreMain {
    public:
        explicit CoreKernel(u64 *args);
        ~CoreKernel() override = default;

        void init() override;

        GdtManager *getGdtManager();
        TssManager *getTssManager();
        IdtManager *getIdtManager();

    private:
        GdtManager coreGdtManager {};
        TssManager coreTssManager {};
        IdtManager coreIdtManager {};

        PIT *corePIT {};

        CpuCore *cpuCore {};
    };
}

#endif