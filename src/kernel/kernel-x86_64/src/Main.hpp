#ifndef KERNEL_X86_64_MAIN_HPP
#define KERNEL_X86_64_MAIN_HPP

#include "CommonMain.hpp"

#include "hal/Cpu.hpp"
#include "hal/GDT.hpp"
#include "hal/IDT.hpp"
#include "hal/KvmClock.hpp"
#include "hal/PIC.hpp"
#include "hal/PIT.hpp"
#include "hal/TSS.hpp"
#include "hal/Hpet.hpp"
#include "memory/X86VirtualMemory.hpp"

namespace kernel::x86_64 {
    using namespace hal;
    using namespace memory;

    using namespace common;
    using namespace common::memory;

    [[noreturn]] void thread1();
    [[noreturn]] void thread2();
    [[noreturn]] void thread3();
    [[noreturn]] void thread4();
    [[noreturn]] void thread5();

    class Kernel final : CommonMain {
    public:
        Kernel();
        ~Kernel() override = default;

        void init() override;

        void shutdown() override;

        GdtManager *getGdtManager();
        TssManager *getTssManager();
        IdtManager *getIdtManager();
        DualPIC *getDualPic();

        PIT *getPIT();

        KvmClock *getKvmClock();

        Hpet *getHpet();

        CpuManager *getCpuManager();

        IOApicManager *getIOApicManager();

    private:
        GdtManager gdtManager {};
        TssManager tssManager {};
        IdtManager idtManager {};

        DualPIC dualPic {};

        PIT pit {};

        KvmClock kvmClock {};

        Hpet hpet {};

        CpuManager cpuManager {};

        IOApicManager ioApicManager {};
    };

    class CoreKernel final : CommonCoreMain {
    public:
        CoreKernel() = default;
        ~CoreKernel() override = default;

        void init() override;

    private:
        GdtManager coreGdtManager {};
        TssManager coreTssManager {};
        IdtManager *coreIdtManager {};

    public:
        CpuCore cpuCore {};
    };
}

#endif