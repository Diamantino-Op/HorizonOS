#include "SpinLock.hpp"

void TicketSpinLock::lock() {
	const auto ticket = __atomic_fetch_add(&nextTicket, 1, __ATOMIC_RELAXED);

	while(__atomic_load_n(&currentTicket, __ATOMIC_ACQUIRE) != ticket) {
		lockedFun();
	}
}

void TicketSpinLock::lockedFun() {
#if defined(__x86_64__)
	asm volatile ("pause" ::: "memory");
#elif defined(__aarch64__)
	asm volatile ("isb" ::: "memory");
#else
	asm volatile ("" ::: "memory");
#endif
}

void TicketSpinLock::unlock() {
	const auto current = __atomic_load_n(&currentTicket, __ATOMIC_RELAXED);

	__atomic_store_n(&currentTicket, current + 1, __ATOMIC_RELEASE);
}

void SimpleSpinLock::lock() {
	while (true) {
		if (!__atomic_exchange_n(&this->locked, true, __ATOMIC_ACQUIRE)) {
			return;
		}

		while (__atomic_load_n(&this->locked, __ATOMIC_RELAXED)) {
			lockedFun();
		}
	}
}

void SimpleSpinLock::lockedFun() {
	#if defined(__x86_64__)
	asm volatile ("pause" ::: "memory");
	#elif defined(__aarch64__)
	asm volatile ("isb" ::: "memory");
	#else
	asm volatile ("" ::: "memory");
	#endif
}

void SimpleSpinLock::unlock() {
	__atomic_store_n(&this->locked, false, __ATOMIC_RELEASE);
}