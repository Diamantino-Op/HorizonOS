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

        void switchContext(u64 *oldCtx, u64 *newCtx);

    private:
        u8 remainingTicks {};

        ThreadListEntry *currentThread {};
    };

    class Scheduler {
    public:
        Scheduler() = default;
        ~Scheduler() = default;

        /**
         *  Get the process with the specified PID.
         *
         *  @param pid The process ID.
         **/
        Process *getProcess(u64 pid);

        /**
         *  Get the thread with the specified TID.
         *
         *  @param process The process where the thread resides.
         *  @param tid The thread ID.
         **/
        Thread *getThread(Process *process, u64 tid);

		/**
		 *  Add a new process to the scheduler.
		 *
		 *  @param process A pointer to the process object to be added.
		 **/
		void addProcess(Process *process);

		/**
		 * Terminate the specified process.
		 *
		 * @param process A pointer to the process to be terminated.
		 **/
		void killProcess(Process *process);

		/**
		 *  Add a thread to the queue.
		 *
		 *  @param thread Pointer to the thread to be added.
		 *  @param priority The priority of the thread.
		 **/
		void addThread(Thread *thread, ProcessPriority priority);

		/**
		 * Terminate the specified thread.
		 *
		 * @param thread A pointer to the thread to be terminated.
		 **/
		void killThread(Thread *thread);

		/**
		 *  Puts the specified thread to sleep for a given number of ticks.
		 *
		 *  @param thread The thread to be put to sleep.
		 *  @param ticks The number of ticks for which the thread should remain asleep.
		 **/
		void sleepThread(Thread *thread, u64 ticks);

		/**
		 *  Create a new context for a thread with the specified parameters.
		 *
		 *  @param isUser Indicates whether the context is for a user-space thread.
		 *  @param rip The instruction pointer for the new context.
		 *  @param rsp The stack pointer for the new context.
		 *
		 *  @return The address of the created context.
		 */
		u64 *createContext(bool isUser, u64 rip, u64 rsp);

    private:
        u64 executionNodesAmount {};

        ExecutionNode *executionNodes {};

        ProcessListEntry *processList {};

        ThreadListEntry *queues[ProcessPriority::COUNT] {};
    };
}

#endif