#ifndef KERNEL_X86_64_CPU_HPP
#define KERNEL_X86_64_CPU_HPP

#include "Types.hpp"

#include "Apic.hpp"
#include "GDT.hpp"
#include "InterruptAllocator.hpp"
#include "TSS.hpp"
#include "Tsc.hpp"
#include "threading/Scheduler.hpp"
#include "utils/ProfilerX86.hpp"

#include "limine.h"

namespace kernel::x86_64 {
	class CoreKernel;
}

namespace kernel::x86_64::hal {
    using namespace common::threading;
	using namespace utils;

    class Apic;
    class Tsc;

    struct CpuCore {
    	u64 kernelStack {};
    	u64 scratchUserRsp {};

        Apic apic {};
        Tsc tsc {};
        ExecutionNode executionNode {};
    	CoreClock coreClock {};

        TssManager *tssManager {};
    	GdtManager *gdtManager {};
    	InterruptAllocator *interruptAllocator {};

    	u32 cpuArrId {};
        u32 cpuId {};
    	u32 lapicId {};

        i64 offset {};

    	u64 currFrame {};

    	bool printEnabled {};

    	u8 lastInt {};

    	u64 schedInt {};
    };

    // TODO: Move to a common file
    class CpuManager {
    public:
        CpuManager() = default;
        ~CpuManager() = default;

        void init();

        void startBootCore();

        void startMultithread();

        auto getCoreAmount() const -> u64;

        auto getCoreList() const -> CoreKernel *;

        auto getBootstrapCpu() const -> CpuCore *;

        static void initSimd();

        static void initSimdContext(const uPtr *ptr);
        static void saveSimdContext(const uPtr *ptr);
        static void loadSimdContext(const uPtr *ptr);

        static void setCorePointer(CpuCore *core);

        __attribute__((no_instrument_function)) static auto getCurrentCore() -> CpuCore *;

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