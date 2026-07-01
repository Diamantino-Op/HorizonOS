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

        __attribute__((no_instrument_function)) static Terminal* getTerminal();

        static CommonMain *getInstance();

        static u64 getCurrentHhdm();

    	static u64 getCurrentRsdp();

        uPtr getStackTop() const;

        AllocContext *getKernelAllocContext() const;

    	AllocContext *getKernelAllocContextHHDM() const;

        PhysicalMemoryManager *getPMM();
        VirtualMemoryManager *getVMM();

    	VirtualPageAllocator *getVPA();

        Clocks *getClocks();

        UAcpi *getUAcpi();

    	AcpiPM *getAcpiPM();

    	Scheduler *getScheduler() const;

    protected:
        void rootInit();

        static CommonMain *instance;

        static Terminal terminal;

    	static u64 currentHhdm;
    	static u64 currentRsdp;

    	bool isInitFlag {};

        uPtr stackTop {};
        AllocContext *kernelAllocContext {};

    	// Workaround because of multicore
    	AllocContext *kernelAllocContextHHDM {};

        PhysicalMemoryManager physicalMemoryManager {};
        VirtualMemoryManager virtualMemoryManager {};

    	VirtualPageAllocator virtualPageAllocator {};

        Clocks clocks {};

        UAcpi uAcpi {};

    	AcpiPM acpiPM {};

    	Scheduler *scheduler {};

    public:
    	static u64 schedulingCoresRemaining;
    };

    class CommonCoreMain {
    public:
        virtual ~CommonCoreMain() = default;

        virtual void init();
    };
}

#endif