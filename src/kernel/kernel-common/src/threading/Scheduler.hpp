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
        u64 getSleepTicks() const;

        void setState(ThreadState state);
        ThreadState getState();

		u16 getId() const;

        Process *getParent();

    private:
        Process *parent {};
        u16 id {};

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
        Process *process {};
        ProcessListEntry *prev {};
    };

    class Process {
    public:
		explicit Process(ProcessPriority priority, bool isUserspace);
    	explicit Process(ProcessPriority priority, AllocContext *context, bool isUserspace);
        ~Process();

        void setPriority(ProcessPriority priority);
        ProcessPriority getPriority();

    	u16 getId() const;

    private:
        u16 id {};

    	bool isUserspace {};

        ThreadListEntry *mainThread {};

        AllocContext *processContext {};

        ProcessPriority priority {};
    };

    class ExecutionNode {
    public:
        ExecutionNode();
        ~ExecutionNode() = default;

        void schedule();

    	ThreadListEntry *getCurrentThread() const;

		void switchThreads();

    	void switchContext(u64 *oldCtx, u64 *newCtx);

    private:
        u8 remainingTicks {};

        ThreadListEntry *currentThread {};
    };

	extern "C" void switchContextAsm(u64 *oldStackPointer, u64 *newStackPointer);

	constexpr u64 threadCtxStackSize = pageSize * 4;

    class Scheduler {
    public:
        Scheduler();
        ~Scheduler() = default;

        /**
         *  Get the process with the specified PID.
         *
         *  @param pid The process ID.
         **/
        Process *getProcess(u16 pid) const;

        /**
         *  Get the thread with the specified TID.
         *
         *  @param process The process where the thread resides.
         *  @param tid The thread ID.
         **/
        Thread *getThread(Process *process, u16 tid) const;

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
		 *
		 *  @return The address of the created context.
		 */
		u64 *createContext(bool isUser, u64 rip);

    private:
    	u64 *createContextArch(bool isUser, u64 rip, u64 rsp);

        u64 executionNodesAmount {};

        ExecutionNode *executionNodes {};

    public:
    	ProcessListEntry *processList {};

    	ThreadListEntry *readyThreadList {};

    	ThreadListEntry *queues[ProcessPriority::COUNT] {};
    	ThreadListEntry *lastQueueEntry[ProcessPriority::COUNT] {};
    };
}

#endif