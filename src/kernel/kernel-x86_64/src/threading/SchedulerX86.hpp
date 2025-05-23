#ifndef KERNEL_X86_64_SCHEDULERX86_HPP
#define KERNEL_X86_64_SCHEDULERX86_HPP

#include "hal/Interrupts.hpp"

#include "memory/VirtualAllocator.hpp"

namespace kernel::x86_64::threading {
    using namespace common::memory;
    using namespace hal;

    class ThreadContext {
    public:
		ThreadContext(u64 stackPointer, bool isUserspace);
        ~ThreadContext();

        u64 *getStackPointer();
        void setStackPointer(u64 stackPtr);

        u64 *getSimdSave() const;

        void save() const;
        void load() const;

        bool isUserspace() const;

    private:
        u64 *originalSimdSave {};
        u64 *simdSave {};

        bool isUser {};

        u64 originalStackPointer {};
        u64 stackPointer {};
    };

    extern "C" void setStackAsm(u64 *stackPointer, u64 rip);
}

#endif
