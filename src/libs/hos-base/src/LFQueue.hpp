#ifndef LIB_HOS_BASE_LFQUEUE_HPP
#define LIB_HOS_BASE_LFQUEUE_HPP

#include "Types.hpp"
#include "stdatomic.h"

template <typename T, usize N>
struct LFQueue {
	static_assert((N & (N - 1)) == 0, "N must be power of two");

	struct Cell {
		alignas(64) usize sequence;
		T value;
	};

	alignas(64) usize head;
	alignas(64) usize tail;
	Cell buf[N];

	void init() {
		__atomic_store_n(&head, 0, __ATOMIC_RELAXED);
		__atomic_store_n(&tail, 0, __ATOMIC_RELAXED);

		for (usize i = 0; i < N; ++i) {
			__atomic_store_n(&buf[i].sequence, i, __ATOMIC_RELAXED);
		}
	}

	bool push(const T& v) {
		Cell *cell;
		usize pos = __atomic_load_n(&head, __ATOMIC_RELAXED);

		for (;;) {
			cell = &buf[pos & (N - 1)];
			const usize sequence = __atomic_load_n(&cell->sequence, __ATOMIC_ACQUIRE);
			const isize diff = static_cast<isize>(sequence) - static_cast<isize>(pos);

			if (diff == 0) {
				if (__atomic_compare_exchange_n(&head, &pos, pos + 1, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
					break;
				}
			} else if (diff < 0) {
				return false;
			} else {
				pos = __atomic_load_n(&head, __ATOMIC_RELAXED);
			}
		}

		cell->value = v;
		__atomic_store_n(&cell->sequence, pos + 1, __ATOMIC_RELEASE);

		return true;
	}

	bool pop(T& out) {
		Cell *cell;
		usize pos = __atomic_load_n(&tail, __ATOMIC_RELAXED);

		for (;;) {
			cell = &buf[pos & (N - 1)];
			const usize sequence = __atomic_load_n(&cell->sequence, __ATOMIC_ACQUIRE);
			const isize diff = static_cast<isize>(sequence) - static_cast<isize>(pos + 1);

			if (diff == 0) {
				if (__atomic_compare_exchange_n(&tail, &pos, pos + 1, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
					break;
				}
			} else if (diff < 0) {
				return false;
			} else {
				pos = __atomic_load_n(&tail, __ATOMIC_RELAXED);
			}
		}

		out = cell->value;
		__atomic_store_n(&cell->sequence, pos + N, __ATOMIC_RELEASE);

		return true;
	}
};

#endif
