#include "SpinLock.hpp"

TicketSpinLock::TicketSpinLock(const TicketSpinLock &) {
	nextTicket.store(0, std::memory_order_relaxed);
	currentTicket.store(0, std::memory_order_relaxed);
}

auto TicketSpinLock::operator=(const TicketSpinLock &) -> TicketSpinLock & {
	nextTicket.store(0, std::memory_order_relaxed);
	currentTicket.store(0, std::memory_order_relaxed);
	return *this;
}

bool TicketSpinLock::lock() {
	u64 flags;

	asm volatile(
		"pushf\n"
		"pop %0\n"
		"cli"
		: "=r"(flags)
		:
		: "memory"
	);

	const auto ticket = nextTicket.fetch_add(1, std::memory_order_relaxed);

	while(currentTicket.load(std::memory_order_acquire) != ticket) {
		lockedFun();
	}

	return flags & (1 << 9);
}

void TicketSpinLock::lockNoCli() {
	const auto ticket = nextTicket.fetch_add(1, std::memory_order_relaxed);

	while(currentTicket.load(std::memory_order_acquire) != ticket) {
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

void TicketSpinLock::unlock(bool prevIF) {
	const auto current = currentTicket.load(std::memory_order_relaxed);

	currentTicket.store(current + 1, std::memory_order_release);

	if (prevIF) {
		asm volatile("sti" ::: "memory");
	}
}

void TicketSpinLock::unlockNoSti() {
	const auto current = currentTicket.load(std::memory_order_relaxed);

	currentTicket.store(current + 1, std::memory_order_release);
}

bool SimpleSpinLock::lock() {
	u64 flags;

	asm volatile(
		"pushf\n"
		"pop %0\n"
		"cli"
		: "=r"(flags)
		:
		: "memory"
	);

	while (true) {
		if (!this->locked.exchange(true, std::memory_order_acquire)) {
			return flags & (1 << 9);
		}

		while (this->locked.load(std::memory_order_relaxed)) {
			lockedFun();
		}
	}
}

SimpleSpinLock::SimpleSpinLock(const SimpleSpinLock &) {
	locked.store(false, std::memory_order_relaxed);
}

auto SimpleSpinLock::operator=(const SimpleSpinLock &) -> SimpleSpinLock & {
	locked.store(false, std::memory_order_relaxed);
	return *this;
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

void SimpleSpinLock::unlock(bool prevIF) {
	this->locked.store(false, std::memory_order_release);

	if (prevIF) {
		asm volatile("sti" ::: "memory");
	}
}
