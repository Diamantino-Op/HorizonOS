#ifndef LIB_HOS_BASE_FUTEX_HPP
#define LIB_HOS_BASE_FUTEX_HPP

#include "LinkedList.hpp"
#include "SpinLock.hpp"
#include "Types.hpp"

struct Waiter {
	u64 address {};
	u16 threadId {};
};

class Futex {
public:
	static bool addWaiter(u64 address, u16 threadId);
	static bool popWaiter(u64 address, u16 *threadId);
	static void removeThread(u16 threadId);

	static TicketSpinLock futexLock;
	static LinkedList<Waiter> *futexWaiters;
};

#endif

