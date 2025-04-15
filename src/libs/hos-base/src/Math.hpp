#ifndef LIB_HOS_BASE_MATH_HPP
#define LIB_HOS_BASE_MATH_HPP

template<class T> T roundUp(T val, T roundTo) {
    return (val + (roundTo - 1)) / roundTo;
}

template <class T> T alignUp(T val, T alignTo) {
    return roundUp<T>(val, alignTo) * alignTo;
}

template<class T> T alignDown(T val, T alignTo) {
    return (val / alignTo) * alignTo;
}

#endif