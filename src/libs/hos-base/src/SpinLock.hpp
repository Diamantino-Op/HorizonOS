#ifndef LIB_HOS_BASE_SPINLOCK_HPP
#define LIB_HOS_BASE_SPINLOCK_HPP

#include "stdbool.h"
#include "Types.hpp"
#include "stdatomic.h"

class SpinLock {
public:
    SpinLock() = default;
    ~SpinLock() = default;

    void lock();
    void unlock();

private:
    void lockedfun();

    u64 nextTicket {};
    u64 currentTicket {};
};

#endif