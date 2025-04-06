#ifndef KERNEL_X86_64_SPINLOCK_HPP
#define KERNEL_X86_64_SPINLOCK_HPP

#include "stdatomic.h"

namespace kernel::common::threads {
    class SpinLock {
    public:
        SpinLock() = default;

        void lock();
        void unlock();

    private:
        atomic_flag lockFlag = ATOMIC_FLAG_INIT;
    };
}

#endif