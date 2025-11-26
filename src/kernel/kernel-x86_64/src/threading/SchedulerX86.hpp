#ifndef KERNEL_X86_64_SCHEDULERX86_HPP
#define KERNEL_X86_64_SCHEDULERX86_HPP

#include "hal/Interrupts.hpp"

#include "memory/VirtualAllocator.hpp"
#include "threading/Scheduler.hpp"

namespace kernel::x86_64::threading {
    using namespace common::memory;
    using namespace common::threading;
    using namespace hal;

    class ThreadContext {
    public:
        ~ThreadContext();

        void init(Process *process, u64 stackPointer, bool isUserspace);

        u64 *getSimdSave() const;

        void save() const;
        void load() const;

        bool isUserspace() const;

    private:
        u64 *originalSimdSave {};
        u64 *simdSave {};

        bool isUser {};

        u64 originalStackPointer {};

        Process *process {};
    };

    extern "C" void setStackAsm(u64 *stackPointer, u64 rip, u64 usermodeFun = 0, u64 userStack = 0);
    extern "C" void threadTrampoline();
}

#endif
