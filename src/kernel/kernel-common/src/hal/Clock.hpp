#ifndef KERNEL_COMMON_CLOCK_HPP
#define KERNEL_COMMON_CLOCK_HPP

#include "Types.hpp"

#include "Vector.hpp"

namespace kernel::common::hal {
    struct Clock {
        char *name {};
        usize priority {};
        u64 (*getNs)() {};
    };
}

#endif