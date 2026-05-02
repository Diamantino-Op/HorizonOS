#ifndef LIB_HOS_BASE_LFQUEUE_HPP
#define LIB_HOS_BASE_LFQUEUE_HPP

#include "Types.hpp"
#include "stdatomic.h"

template <typename T, usize N>
struct LFQueue {
	static_assert((N & (N - 1)) == 0, "N must be power of two");

	alignas(64) volatile usize head;
	alignas(64) volatile usize tail;
	T buf[N];

	void init() {
		__atomic_store_n(&head, 0, __ATOMIC_RELAXED);
		__atomic_store_n(&tail, 0, __ATOMIC_RELAXED);
	}

	bool push(const T& v) {
		usize h, next;
		do {
			h = __atomic_load_n(&head, __ATOMIC_RELAXED);
			next = (h + 1) & (N - 1);
			usize t = __atomic_load_n(&tail, __ATOMIC_ACQUIRE);

			if (next == t) {
				return false;
			}

			buf[h] = v;
		} while (!__atomic_compare_exchange_n(&head, &h, next, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED));

		return true;
	}

	bool pop(T& out) {
		usize t = __atomic_load_n(&tail, __ATOMIC_RELAXED);
		usize h = __atomic_load_n(&head, __ATOMIC_ACQUIRE);

		if (t == h) {
			return false;
		}

		out = *(reinterpret_cast<T*>(reinterpret_cast<char*>(buf) + t * sizeof(T)));

		out = buf[t];

		__atomic_store_n(&tail, (t + 1) & (N - 1), __ATOMIC_RELEASE);

		return true;
	}
};

#endif
