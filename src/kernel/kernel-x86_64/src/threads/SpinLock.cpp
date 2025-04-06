#include "threads/SpinLock.hpp"

namespace kernel::common::threads {
	void SpinLock::lock() {
		while(atomic_flag_test_and_set_explicit(&this->lockFlag, memory_order_acquire)) { // Use __asm__ volatile ("yield"); for ARM
			asm volatile("pause");
		}
	}

	void SpinLock::unlock() {
		atomic_flag_clear_explicit(&this->lockFlag, memory_order_release);
	}
}