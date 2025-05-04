#ifndef KERNEL_COMMON_SCHEDULER_HPP
#define KERNEL_COMMON_SCHEDULER_HPP

#include "Types.hpp"

namespace kernel::common::threading {
    class ExecutionNode {

    };

    class Scheduler {
    public:
        Scheduler() = default;
        ~Scheduler() = default;

    private:
        u64 executionNodesAmount {};

        ExecutionNode *executionNodes {};
    };
}

#endif