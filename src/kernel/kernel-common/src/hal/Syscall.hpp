#ifndef KERNEL_COMMON_SYSCALL_HPP
#define KERNEL_COMMON_SYSCALL_HPP

#include "Types.hpp"

namespace kernel::common::hal {
    class SyscallManager {
    public:
        void init();
    };

    extern "C" void syscallHandler();
}

#endif