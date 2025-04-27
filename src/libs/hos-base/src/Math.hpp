#ifndef LIB_HOS_BASE_MATH_HPP
#define LIB_HOS_BASE_MATH_HPP

#include "Types.hpp"

template<class T> T roundUp(T val, T roundTo) {
    return (val + (roundTo - 1)) / roundTo;
}

template <class T> T alignUp(T val, T alignTo) {
    return roundUp<T>(val, alignTo) * alignTo;
}

template <class T> T alignDown(T val, T alignTo) {
	return (val / alignTo) * alignTo;
}

inline u64 ns2Ticks(const u64 ns, const u64 p, const u64 n) {
    return (ns << p) / n;
}

inline u64 ticks2ns(const u128 ticks, const u64 p, const u64 n) {
    return (ticks * n) >> p;
}

template <class T> T log2(T val) {
    return reinterpret_cast<T>((val) - 1);
}

inline auto pow2(const usize val) {
    return 1ull << val;
}

inline Pair freq2NsPN(const u64 freq) {
	const u64 p = log2<u64>(freq);
	const u64 n = (1'000'000'000ull << p) / freq;

    return { p, n };
}

#endif