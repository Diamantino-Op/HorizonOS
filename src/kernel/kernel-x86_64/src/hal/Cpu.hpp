#ifndef KERNEL_X86_64_CPU_HPP
#define KERNEL_X86_64_CPU_HPP

#define LIMINE_API_REVISION 3

#include "Types.hpp"

#include "Apic.hpp"
#include "Tsc.hpp"

#include "threading/Scheduler.hpp"

#include "limine.h"

namespace kernel::x86_64 {
	class CoreKernel;
}

namespace kernel::x86_64::hal {
    using namespace common::threading;
    class Apic;
    class Tsc;

    struct CpuCore {
        Apic apic {};
        Tsc tsc {};
        ExecutionNode executionNode {};

        u32 cpuId {};

        i64 offset {};
    };

    class CpuManager {
    public:
        CpuManager() = default;
        ~CpuManager() = default;

        void init();

        void startMultithread();

        u64 getCoreAmount() const;

        CoreKernel *getCoreList() const;

        CpuCore *getBootstrapCpu() const;

        static void initSimd();

        static void initSimdContext(const uPtr *ptr);
        static void saveSimdContext(const uPtr *ptr);
        static void loadSimdContext(const uPtr *ptr);

        static void setCorePointer(CpuCore *core);

        static CpuCore *getCurrentCore();

    private:
        void initCore(u64 coreId, u64 listIndex) const;

        u64 coreAmount {};
        CoreKernel *cpuList {};

        CpuCore *bootstrapCpu {};

        char *brand {};
        char *vendor {};

        bool hasX2Apic {};
    };

    void bootCore(const limine_mp_info *info);
}

#endif