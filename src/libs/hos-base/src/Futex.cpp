#include "Futex.hpp"

TicketSpinLock Futex::futexLock {};
LinkedList<Waiter> *Futex::futexWaiters {};

namespace {
	LinkedList<Waiter> &getFutexWaiters() {
		if (Futex::futexWaiters == nullptr) {
			Futex::futexWaiters = new LinkedList<Waiter>();
		}

		return *Futex::futexWaiters;
	}
}

bool Futex::addWaiter(const u64 address, const u16 threadId) {
	const bool prevIF = futexLock.lock();
	auto &waiters = getFutexWaiters();

	for (auto &waiter : waiters) {
		if (waiter.threadId == threadId) {
			futexLock.unlock(prevIF);
			return true;
		}
	}

	auto *waiter = new Waiter();

	if (waiter == nullptr) {
		futexLock.unlock(prevIF);
		return false;
	}

	waiter->address = address;
	waiter->threadId = threadId;
	waiters.addEnd(waiter);

	futexLock.unlock(prevIF);
	return true;
}

bool Futex::popWaiter(const u64 address, u16 *threadId) {
	if (threadId == nullptr) {
		return false;
	}

	const bool prevIF = futexLock.lock();
	auto &waiters = getFutexWaiters();
	auto *entry = waiters.getFirst();

	while (entry != nullptr) {
		auto *next = entry->next;
		auto *waiter = entry->value;

		if (waiter != nullptr && waiter->address == address) {
			*threadId = waiter->threadId;
			waiters.remove(entry, false);
			delete waiter;
			futexLock.unlock(prevIF);
			return true;
		}

		entry = next;
	}

	futexLock.unlock(prevIF);
	return false;
}

bool Futex::removeWaiter(const u64 address, const u16 threadId) {
	const bool prevIF = futexLock.lock();
	auto &waiters = getFutexWaiters();
	auto *entry = waiters.getFirst();

	while (entry != nullptr) {
		auto *next = entry->next;
		auto *waiter = entry->value;

		if (waiter != nullptr && waiter->address == address && waiter->threadId == threadId) {
			waiters.remove(entry, false);
			delete waiter;
			futexLock.unlock(prevIF);
			return true;
		}

		entry = next;
	}

	futexLock.unlock(prevIF);
	return false;
}

void Futex::removeThread(const u16 threadId) {
	const bool prevIF = futexLock.lock();
	auto &waiters = getFutexWaiters();
	auto *entry = waiters.getFirst();

	while (entry != nullptr) {
		auto *next = entry->next;
		auto *waiter = entry->value;

		if (waiter != nullptr && waiter->threadId == threadId) {
			waiters.remove(entry, false);
			delete waiter;
		}

		entry = next;
	}

	futexLock.unlock(prevIF);
}


