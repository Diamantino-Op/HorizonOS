#ifndef KERNEL_X86_64_SCHEDULERX86_HPP
#define KERNEL_X86_64_SCHEDULERX86_HPP

#include "hal/TSS.hpp"
#include "memory/VirtualAllocator.hpp"
#include "threading/Scheduler.hpp"

namespace kernel::x86_64::threading {
    using namespace common::memory;
    using namespace common::threading;
    using namespace hal;

    class ThreadContext {
    public:
        ~ThreadContext();

    	bool init(Process *proc, u64 stackPointer, bool isUserspace);

        u64 *getSimdSave() const;
        u64 *getOriginalSimdSave() const;
        u64 getSimdSaveAllocSize() const;
        bool isValid() const;

        void save();
        void load() const;

        bool isUserspace() const;

    	void updateTssPtrs(u64 rsp0);

    private:
        u64 magic {};
        u64 simdSaveAllocSize {};
        u64 *originalSimdSave {};
        u64 *simdSave {};

        bool isUser {};
        u64 userGsBase {};
    	u64 userFsBase {};

        u64 originalStackPointer {};

        Process *process {};

    public:
    	u8 prid {};

    	u64 userStackPointer {};

        TssIopb *threadTssIopb {};
    };

    extern "C" u64 checkDisabled();
    extern "C" u128 scheduleEntry(u64 oldRsp);
    extern "C" void loadNewThread();
    extern "C" void finishScheduleSwitch();
    extern "C" void kernelThreadReturned() __attribute__((noreturn));
    extern "C" void destroyThreadContext(u64 *context, Process *process);
    extern "C" void setStackAsm(u64 *stackPointer, u64 rip, u64 usermodeFun = 0, u64 userStack = 0);
    extern "C" void setUserStackAsm(u64 *stackPointer);
    extern "C" void kernelThreadTrampoline();
    extern "C" void threadTrampoline32();
    extern "C" void threadTrampoline64();
}

#endif
