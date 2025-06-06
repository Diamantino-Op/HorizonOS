#ifndef KERNEL_COMMON_MMIO_HPP
#define KERNEL_COMMON_MMIO_HPP

#include "Types.hpp"

namespace kernel::common::utils {
    class MMIO {
    public:
        static u64 read(u64 addr, usize width);
        static void write(u64 addr, u64 data, usize width);

        template<class T> static T in(const u64 addr) {
            return read(addr, sizeof(T));
        }

        template<class T> static void out(u64 addr, T data) {
            write(addr, data, sizeof(T));
        }
    };
}

#endif