#ifndef KERNEL_COMMON_SCHEDULER_HPP
#define KERNEL_COMMON_SCHEDULER_HPP

#include "Types.hpp"
#include "memory/VirtualAllocator.hpp"

namespace kernel::common::threading {
    using namespace memory;

    constexpr u8 maxTicks = 50; // 50ms with PIT at 1kHz

    enum ThreadState {
        READY,
        RUNNING,
        BLOCKED,
        TERMINATED
    };

    enum ProcessPriority : u8 {
        VERY_HIGH = 0,
        HIGH = 1,
        NORMAL = 2,
        LOW = 3,
        COUNT = 4
    };

    class Process;

    class Thread {
    public:
		explicit Thread(Process* parent);
        ~Thread();

        void setContext(u64 *context);
        u64 *getContext();

        void setSleepTicks(u64 ticks);
        u64 getSleepTicks();

        void setState(ThreadState state);
        ThreadState getState();

        Process *getParent();

    private:
        Process *parent {};
        u64 id {};

        u64 sleepTicks {};

        u64 stackPointer {};

        u64 *context {};
        ThreadState state {};
    };

    struct ThreadListEntry {
        ThreadListEntry *next {};
        Thread *thread {};
        ThreadListEntry *prev {};
    };

    struct ProcessListEntry {
        ProcessListEntry *next {};
        Process *thread {};
        ProcessListEntry *prev {};
    };

    class Process {
    public:
		explicit Process(ProcessPriority priority);
        ~Process();

        void setPriority(ProcessPriority priority);
        ProcessPriority getPriority();

    private:
        u64 id {};

        ThreadListEntry *mainThread {};

        AllocContext processContext {};

        ProcessPriority priority {};
    };

    class ExecutionNode {
    public:
        ExecutionNode();
        ~ExecutionNode();

        void schedule();

        void switchContextAsm(u64 *oldCtx, u64 *newCtx);

    private:
        u8 remainingTicks {};

        ThreadListEntry *currentThread {};
    };

    class Scheduler {
    public:
        Scheduler() = default;
        ~Scheduler() = default;

        Process *getProcess(u64 pid);
        Thread *getThread(Process *process, u64 tid);

        void addProcess(Process *process);
        void killProcess(Process *process);

        void addThread(Thread *thread);
        void killThread(Thread *thread);

        void sleepThread(Thread *thread, u64 ticks);

        u64 *createContext(bool isUser, u64 rip, u64 rsp);

    private:
        u64 executionNodesAmount {};

        ExecutionNode *executionNodes {};

        ProcessListEntry *processList {};

        ThreadListEntry *queues[ProcessPriority::COUNT] {};
    };
}

#endif