#ifndef KERNEL_COMMON_COMMONMAIN_HPP
#define KERNEL_COMMON_COMMONMAIN_HPP

#define LIMINE_API_REVISION 3

#include "Terminal.hpp"

#include "memory/VirtualAllocator.hpp"

namespace kernel::common {
    using namespace memory;

    struct CommonCoreArgs {
        Terminal *terminal {};

        u64 currentHhdm {};

        AllocContext *kernelAllocContext {};

        PhysicalMemoryManager *pmm {};
        VirtualMemoryManager *vmm {};
    };

    class CommonMain {
    public:
		virtual ~CommonMain() = default;

		virtual void init();

        static Terminal* getTerminal();

        static CommonMain *getInstance();

        static u64 getCurrentHhdm();

        AllocContext *getKernelAllocContext() const;

        PhysicalMemoryManager *getPMM();
        VirtualMemoryManager *getVMM();

    protected:
        virtual void halt();

        void rootInit();

        static CommonMain *instance;

        static Terminal terminal;

        uPtr stackTop {};
        AllocContext *kernelAllocContext {};

        PhysicalMemoryManager physicalMemoryManager;
        VirtualMemoryManager virtualMemoryManager;
    };

    class CommonCoreMain {
    public:
        virtual ~CommonCoreMain() = default;

        virtual void init();

        static Terminal* getTerminal();

        static u64 getCurrentHhdm();

        AllocContext *getKernelAllocContext() const;

        PhysicalMemoryManager *getPMM();
        VirtualMemoryManager *getVMM();

        static void halt();

    protected:
        void rootInit();

        static CommonCoreMain *instance;

        static Terminal *terminal;

        static u64 currentHhdm;

        AllocContext *kernelAllocContext {};

        PhysicalMemoryManager *corePhysicalMemoryManager {};
        VirtualMemoryManager *coreVirtualMemoryManager {};
    };
}

#endif