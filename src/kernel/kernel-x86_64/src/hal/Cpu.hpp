#ifndef KERNEL_X86_64_CPU_HPP
#define KERNEL_X86_64_CPU_HPP

#include "Types.hpp"

#include "Apic.hpp"

namespace kernel::x86_64::hal {
    struct CpuCore {
        Apic apic;

        u32 cpuId;
    };

    class CpuManager {
    public:
        CpuManager() = default;
        ~CpuManager() = default;

        void init();

    private:
        void initSimd() const;

        u64 coreAmount {};
        CpuCore *cpuList {};

        Apic *bootstrapApic {};

        char *brand {};
        char *vendor {};

        bool hasX2Apic {};
    };

    void coreInit(const Cpu *cpu);
}

#endif