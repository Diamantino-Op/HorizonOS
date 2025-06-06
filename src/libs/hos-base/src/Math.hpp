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
    if (val == 0) {
        return 0;
    }

    if (sizeof(T) <= sizeof(u32)) {
        return reinterpret_cast<T>(__builtin_clz(static_cast<u32>(val)) ^ (sizeof(u32) * 8 - 1));
    }

    if (sizeof(T) <= sizeof(u64)) {
        return reinterpret_cast<T>(__builtin_clzl(static_cast<u64>(val)) ^ (sizeof(u64) * 8 - 1));
    }

    if (sizeof(T) <= sizeof(u128)) {
        return reinterpret_cast<T>(__builtin_clzll(static_cast<u128>(val)) ^ (sizeof(u128) * 8 - 1));
    }

    T result = 0;

    while (val >>= 1) {
        ++result;
    }

    return result;
}

inline u64 pow2(const u64 val) {
    return 1ull << val;
}

inline Pair freq2NsPN(const u64 freq) {
	const u64 p = log2<u64>(freq);
	const u64 n = (1'000'000'000ull << p) / freq;

    return { p, n };
}

inline u32 popCountByte(u8 b) {

}

inline u64 highestBitPosition(u64 val) {

}

inline u64 ceilingPow2(u64 val) {

}

inline u64 sqrt(u64 f) {

}

#endif