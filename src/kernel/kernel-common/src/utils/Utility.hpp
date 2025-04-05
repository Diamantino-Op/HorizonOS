#ifndef KERNEL_COMMON_UTILITY_HPP
#define KERNEL_COMMON_UTILITY_HPP

#include "TypeTraits.hpp"

namespace kernel::common {
    using namespace lib::hos::base;

    template<class T> constexpr removeReferenceT<T> &&move(T &&t) noexcept {
        return static_cast<removeReferenceT<T> &&>(t);
    }

    template<class T> constexpr T &&forward(removeReferenceT<T> &t) noexcept {
        return static_cast<T &&>(t);
    }

    template<class T> constexpr T &&forward(removeReferenceT<T> &&t) noexcept {
        static_assert(!isLvalueReferenceV<T>, "Not lvalue reference\n");
        return static_cast<T &&>(t);
    }

    template<class T> constexpr void swap(T &a, T &b) {
        T temp(move(a));
        a = move(b);
        b = move(temp);
    }
}

#endif