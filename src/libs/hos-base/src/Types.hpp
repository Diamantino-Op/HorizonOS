#ifndef TYPES_HPP
#define TYPES_HPP

extern "C" {
    typedef __INT8_TYPE__ i8;
    typedef __INT16_TYPE__ i16;
    typedef __INT32_TYPE__ i32;
    typedef __INT64_TYPE__ i64;

    typedef __UINT8_TYPE__ u8;
    typedef __UINT16_TYPE__ u16;
    typedef __UINT32_TYPE__ u32;
    typedef __UINT64_TYPE__ u64;

    typedef __INTPTR_TYPE__ iPtr;
    typedef __UINTPTR_TYPE__ uPtr;

    typedef __INTMAX_TYPE__ iMax;
    typedef __UINTMAX_TYPE__ uMax;

    typedef __INT_LEAST8_TYPE__ iLeast8;
    typedef __INT_LEAST16_TYPE__ iLeast16;
    typedef __INT_LEAST32_TYPE__ iLeast32;
    typedef __INT_LEAST64_TYPE__ iLeast64;

    typedef __UINT_LEAST8_TYPE__ uLeast8;
    typedef __UINT_LEAST16_TYPE__ uLeast16;
    typedef __UINT_LEAST32_TYPE__ uLeast32;
    typedef __UINT_LEAST64_TYPE__ uLeast64;

    typedef __INT_FAST8_TYPE__ iFast8;
    typedef __INT_FAST16_TYPE__ iFast16;
    typedef __INT_FAST32_TYPE__ iFast32;
    typedef __INT_FAST64_TYPE__ iFast64;

    typedef __UINT_FAST8_TYPE__ uFast8;
    typedef __UINT_FAST16_TYPE__ uFast16;
    typedef __UINT_FAST32_TYPE__ uFast32;
    typedef __UINT_FAST64_TYPE__ uFast64;
}

#endif
