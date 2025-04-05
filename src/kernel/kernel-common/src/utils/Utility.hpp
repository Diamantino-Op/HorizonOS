#ifndef KERNEL_COMMON_UTILITY_HPP
#define KERNEL_COMMON_UTILITY_HPP

namespace kernel::common {
    template<class T> constexpr RemoveReferenceT<T> &&move(T &&t) noexcept {}
}

#endif