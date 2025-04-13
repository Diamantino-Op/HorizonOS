#ifndef KERNEL_COMMON_COMMONMAIN_HPP
#define KERNEL_COMMON_COMMONMAIN_HPP

#define LIMINE_API_REVISION 3

#include "Terminal.hpp"

namespace kernel::common {
    class CommonMain {
    public:
		virtual void init();

        static Terminal* getTerminal();

    protected:
        virtual void halt();

        static Terminal terminal;

        uPtr stackTop {};
    };
}

#endif