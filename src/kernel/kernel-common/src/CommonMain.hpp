#ifndef KERNEL_COMMON_COMMONMAIN_HPP
#define KERNEL_COMMON_COMMONMAIN_HPP

#define LIMINE_API_REVISION 3

#include "Terminal.hpp"

#include "memory/VirtualAllocator.hpp"
#include "hal/Clock.hpp"
#include "uacpi/UacpiKernAPI.hpp"

namespace kernel::common {
    using namespace memory;
    using namespace uacpi;
    using namespace hal;

    class CommonMain {
    public:
		virtual ~CommonMain() = default;

		virtual void init();

        virtual void shutdown();

        static Terminal* getTerminal();

        static CommonMain *getInstance();

        static u64 getCurrentHhdm();

        [[nodiscard]] uPtr getStackTop() const;

        [[nodiscard]] AllocContext *getKernelAllocContext() const;

        PhysicalMemoryManager *getPMM();
        VirtualMemoryManager *getVMM();

        Clocks *getClocks();

        UAcpi *getUAcpi();

    protected:
        void rootInit();

        static CommonMain *instance;

        static Terminal terminal;

        uPtr stackTop {};
        AllocContext *kernelAllocContext {};

        PhysicalMemoryManager physicalMemoryManager {};
        VirtualMemoryManager virtualMemoryManager {};

        Clocks clocks {};

        UAcpi uAcpi {};
    };

    class CommonCoreMain {
    public:
        virtual ~CommonCoreMain() = default;

        virtual void init();
    };
}

#endif