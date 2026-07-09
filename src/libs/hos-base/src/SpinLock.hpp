#ifndef LIB_HOS_BASE_SPINLOCK_HPP
#define LIB_HOS_BASE_SPINLOCK_HPP

#include "Types.hpp"

#include <atomic>

// TODO: Maybe disable ints when locking and re-enable when unlocking

class TicketSpinLock {
public:
    TicketSpinLock() = default;
    TicketSpinLock(const TicketSpinLock &);
    ~TicketSpinLock() = default;

	auto operator=(const TicketSpinLock &) -> TicketSpinLock &;

    bool lock();
	void lockNoCli();

    void unlock(bool prevIF);
	void unlockNoSti();

private:
    void lockedFun();

    std::atomic<u64> nextTicket {};
    std::atomic<u64> currentTicket {};
};

class SimpleSpinLock {
public:
    SimpleSpinLock() = default;
	SimpleSpinLock(const SimpleSpinLock &);
    ~SimpleSpinLock() = default;

	auto operator=(const SimpleSpinLock &) -> SimpleSpinLock &;

    bool lock();
    void unlock(bool prevIF);

private:
    void lockedFun();

    std::atomic<bool> locked {};
};

#endif
