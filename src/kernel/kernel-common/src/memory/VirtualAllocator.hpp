#ifndef KERNEL_COMMON_VIRTUALALLOCATOR_HPP
#define KERNEL_COMMON_VIRTUALALLOCATOR_HPP

#include "VirtualMemory.hpp"

namespace kernel::common::memory {
    struct AllocContext {
        PageMap pageMap {};
    };
}

#endif