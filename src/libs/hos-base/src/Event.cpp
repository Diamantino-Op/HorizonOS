#include "Event.hpp"

SimpleEvent::SimpleEvent(const u64 counter) {
	atomic_init(&this->counter, counter);
}

bool SimpleEvent::decrement() {
	u64 val;

	while (true) {
		val = atomic_load_explicit(&this->counter, memory_order_acquire);

		if (val == 0) {
			return false;
		}

		if (atomic_compare_exchange_strong_explicit(&this->counter, &val, val - 1, memory_order_acq_rel, memory_order_acquire)) {
			return true;
		}
	}
}

void SimpleEvent::add(const u64 val) {
	atomic_fetch_add_explicit(&this->counter, val, memory_order_acq_rel);
}

void SimpleEvent::set(const u64 val) {
	atomic_store_explicit(&this->counter, val, memory_order_release);
}