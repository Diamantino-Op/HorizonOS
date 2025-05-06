#ifndef KERNEL_X86_64_SCHEDULERX86_HPP
#define KERNEL_X86_64_SCHEDULERX86_HPP

#include "hal/Interrupts.hpp"

#include "memory/VirtualAllocator.hpp"

namespace kernel::x86_64::threading {
    using namespace common::memory;
    using namespace hal;

    class ThreadContext {
    public:
		ThreadContext();
        ~ThreadContext();

        Frame *getFrame();

        void save() const;
        void load() const;

    private:
        u64 *simdSave {};

        Frame frame {};
    };
}

#endif
