#include "SpinLock.hpp"

void SpinLock::lock() {
	while(atomic_flag_test_and_set_explicit(&isLocked, memory_order_acquire)) {}
}

void SpinLock::unlock() {
	atomic_flag_clear_explicit(&isLocked, memory_order_release);
}