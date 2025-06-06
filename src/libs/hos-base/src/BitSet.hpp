#ifndef LIB_HOS_BITSET_HPP
#define LIB_HOS_BITSET_HPP

#include "Types.hpp"

struct BitSetRange {
    u64 fromBucket {};
    u64 toBucket {};

    u8 fromIndex {};
    u8 toIndex {};
};

class BitSet {
public:
    static u64 bitSetSizeOf(u64 elements);

    static BitSetRange range(u64 fromPos, u64 toPos);

    static void setRange(u8 *bitSet, BitSetRange range);

    static void clearRange(u8 *bitSet, BitSetRange range);

    static u64 countRange(u8 *bitSet, BitSetRange range);

    static void set(u8 *bitSet, u64 pos);

    static void clear(u8 *bitSet, u64 pos);

    static bool test(u8 *bitSet, u64 pos);

    static void shiftLeft(u8 *bitSet, u64 fromPos, u64 toPos, u64 by);

    static void shiftRight(u8 *bitSet, u64 fromPos, u64 toPos, u64 by);

    static void debug(u8 *bitSet, u64 length);
};

#endif
