#ifndef KERNEL_COMMON_COMMONMAIN_HPP
#define KERNEL_COMMON_COMMONMAIN_HPP

#include "Terminal.hpp"

namespace kernel::common {
    class CommonMain {
    public:
		virtual void init();

        static Terminal* getTerminal();

        static uPtr getStackTop();

    protected:
        virtual void halt();

        static Terminal terminal;

        static uPtr stackTop;
    };
}

#endif