#ifndef KERNEL_COMMON_COMMONMAIN_HPP
#define KERNEL_COMMON_COMMONMAIN_HPP

#include "Terminal.hpp"

#include "memory/VirtualAllocator.hpp"
#include "hal/Clock.hpp"
#include "hal/AcpiPM.hpp"
#include "uacpi/UacpiKernAPI.hpp"
#include "threading/Scheduler.hpp"

namespace kernel::common {
    using namespace memory;
    using namespace uacpi;
    using namespace hal;
	using namespace threading;

    class CommonMain {
    public:
		virtual ~CommonMain() = default;

		virtual void init();

        virtual void shutdown();

    	bool isInit() const;

        static Terminal* getTerminal();

        static CommonMain *getInstance();

        static u64 getCurrentHhdm();

        [[nodiscard]] uPtr getStackTop() const;

        [[nodiscard]] AllocContext *getKernelAllocContext() const;

        PhysicalMemoryManager *getPMM();
        VirtualMemoryManager *getVMM();

        Clocks *getClocks();

        UAcpi *getUAcpi();

    	AcpiPM *getAcpiPM();

    	Scheduler *getScheduler() const;

    protected:
        void rootInit();

        static CommonMain *instance;

        static Terminal terminal;

    	bool isInitFlag {};

        uPtr stackTop {};
        AllocContext *kernelAllocContext {};

        PhysicalMemoryManager physicalMemoryManager {};
        VirtualMemoryManager virtualMemoryManager {};

        Clocks clocks {};

        UAcpi uAcpi {};

    	AcpiPM acpiPM {};

    	Scheduler *scheduler {};
    };

    class CommonCoreMain {
    public:
        virtual ~CommonCoreMain() = default;

        virtual void init();
    };
}

#endif