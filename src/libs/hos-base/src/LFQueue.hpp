#ifndef LIB_HOS_BASE_LFQUEUE_HPP
#define LIB_HOS_BASE_LFQUEUE_HPP

#include "Types.hpp"

#include <atomic>
#include <new>

template <typename T, usize N>
struct LFQueue {
	static_assert((N & (N - 1)) == 0, "N must be power of two");

	// Bounded MPSC queue: many producers may call push(), exactly one consumer
	// may call pop(). init() must not run while producers or the consumer use it.
	struct Cell {
		alignas(std::hardware_destructive_interference_size) std::atomic<usize> sequence {0};
		T value;
	};

	alignas(std::hardware_destructive_interference_size) std::atomic<usize> head {0};
	alignas(std::hardware_destructive_interference_size) usize tail {0};
	Cell buf[N] {};

	LFQueue() = default;

	LFQueue(const LFQueue &) {
		init();
	}

	auto operator=(const LFQueue &) -> LFQueue & {
		init();
		return *this;
	}

	void init() {
		head.store(0, std::memory_order_relaxed);
		tail = 0;

		for (usize i = 0; i < N; ++i) {
			buf[i].sequence.store(i, std::memory_order_relaxed);
		}
	}

	bool push(const T& v) {
		Cell *cell;
		usize pos = head.load(std::memory_order_relaxed);

		for (;;) {
			cell = &buf[pos & (N - 1)];
			const usize sequence = cell->sequence.load(std::memory_order_acquire);
			const isize diff = static_cast<isize>(sequence) - static_cast<isize>(pos);

			if (diff == 0) {
				if (head.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed, std::memory_order_relaxed)) {
					break;
				}
			} else if (diff < 0) {
				return false;
			} else {
				pos = head.load(std::memory_order_relaxed);
			}
		}

		cell->value = v;
		cell->sequence.store(pos + 1, std::memory_order_release);

		return true;
	}

	bool pop(T& out) {
		const usize pos = tail;
		Cell *cell = &buf[pos & (N - 1)];
		const usize sequence = cell->sequence.load(std::memory_order_acquire);
		const isize diff = static_cast<isize>(sequence) - static_cast<isize>(pos + 1);

		if (diff != 0) {
			return false;
		}

		out = cell->value;
		cell->sequence.store(pos + N, std::memory_order_release);
		tail = pos + 1;

		return true;
	}
};

#endif
