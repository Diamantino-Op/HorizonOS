#ifndef LIB_HOS_BASE_SPINLOCK_HPP
#define LIB_HOS_BASE_SPINLOCK_HPP

#include "stdbool.h"
#include "Types.hpp"
#include "stdatomic.h"

// TODO: Maybe disable ints when locking and re-enable when unlocking

class TicketSpinLock {
public:
    TicketSpinLock() = default;
    ~TicketSpinLock() = default;

    void lock();
    void unlock();

private:
    void lockedFun();

    u64 nextTicket {};
    u64 currentTicket {};
};

class SimpleSpinLock {
public:
    SimpleSpinLock() = default;
    ~SimpleSpinLock() = default;

    void lock();
    void unlock();

private:
    void lockedFun();

    bool locked {};
};

#endif