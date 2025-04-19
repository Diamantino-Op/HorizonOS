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
		CommonMain();

		virtual void init();

        static Terminal* getTerminal();

        static CommonMain *getInstance();

        AllocContext *getKernelAllocContext();

    protected:
        virtual void halt();

        static CommonMain *instance;

        static Terminal terminal;

        uPtr stackTop {};
        AllocContext kernelAllocContext {};
    };
}

#endif