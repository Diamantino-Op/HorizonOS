#ifndef LIB_HOS_BASE_SPINLOCK_HPP
#define LIB_HOS_BASE_SPINLOCK_HPP

#include "stdbool.h"
#include "stdatomic.h"

class SpinLock {
public:
    SpinLock() = default;
    ~SpinLock() = default;

    void lock();
    void unlock();

private:
    atomic_flag isLocked = static_cast<atomic_flag>(false);
};

#endif