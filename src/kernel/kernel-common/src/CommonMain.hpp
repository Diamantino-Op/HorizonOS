#ifndef KERNEL_COMMON_COMMONMAIN_HPP
#define KERNEL_COMMON_COMMONMAIN_HPP

#define LIMINE_API_REVISION 3

#include "Terminal.hpp"

#include "memory/VirtualAllocator.hpp"

namespace kernel::common {
    using namespace memory;

    class CommonMain {
    public:
		virtual ~CommonMain() = default;

		virtual void init();

        static Terminal* getTerminal();

        static CommonMain *getInstance();

        static u64 getCurrentHhdm();

        uPtr getStackTop() const;

        AllocContext *getKernelAllocContext() const;

        PhysicalMemoryManager *getPMM();
        VirtualMemoryManager *getVMM();

    protected:
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
    };
}

#endif