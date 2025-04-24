#include "SpinLock.hpp"

void SpinLock::lock() {
	const auto ticket = __atomic_fetch_add(&nextTicket, 1, __ATOMIC_RELAXED);

	while(__atomic_load_n(&currentTicket, __ATOMIC_ACQUIRE) != ticket) {
		lockedfun();
	}
}

void SpinLock::lockedfun() {
#if defined(__x86_64__)
	asm volatile ("pause" ::: "memory");
#elif defined(__aarch64__)
	asm volatile ("isb" ::: "memory");
#else
	asm volatile ("" ::: "memory");
#endif
}

void SpinLock::unlock() {
	const auto current = __atomic_load_n(&currentTicket, __ATOMIC_RELAXED);

	__atomic_store_n(&currentTicket, current + 1, __ATOMIC_RELEASE);
}