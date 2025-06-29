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
		explicit Thread(Process* parent, u64 *context);
        ~Thread();

        void setContext(u64 *context);
        u64 *getContext() const;

        void setSleepNs(u64 ns);
        u64 getSleepNs() const;

        void setState(ThreadState state);
        ThreadState getState() const;

		u16 getId() const;

        Process *getParent() const;

    private:
        Process *parent {};
        u16 id {};

        u64 sleepNs {};

        u64 *context {};
        ThreadState state {};
    };

    struct ThreadListEntry {
    	ThreadListEntry *nextProc {};
        ThreadListEntry *next {};
        Thread *thread {};
        ThreadListEntry *prev {};
    	ThreadListEntry *prevProc {};
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
        ProcessPriority getPriority() const;

    	AllocContext *getProcessContext() const;

    	void addThread(ThreadListEntry *entry);

    	u16 getId() const;

    private:
        u16 id {};

    	bool isUserspace {};

        ThreadListEntry *mainThread {};

    	ThreadListEntry *threadList {};
    	ThreadListEntry *lastThreadList {};

        AllocContext *processContext {};

        ProcessPriority priority {};
    };

    class ExecutionNode {
    public:
        ExecutionNode() = default;
        ~ExecutionNode() = default;

    	void init();

        void schedule();

    	void setCurrentThread(ThreadListEntry *thread);
    	ThreadListEntry *getCurrentThread() const;

		void switchThreads();

    	void switchContext(u64 *oldCtx, u64 *newCtx) const;

    	bool isDisabled() const;
    	void setDisabled(bool val);

    private:
    	static u32 scheduleTick(u64 *);

    	void initArch();

		bool isDisabledFlag;

        ThreadListEntry *currentThread {};
    };

	void idleThread();

	extern "C" void switchContextAsm(u64 *oldStackPointer, u64 *newStackPointer);

	struct SleepQueueEntry {
		SleepQueueEntry *prev {};
		Thread *thread {};
		SleepQueueEntry *next {};
	};

	constexpr u64 threadCtxStackSize = pageSize * 4;

    class Scheduler {
    public:
        Scheduler();
        ~Scheduler() = default;

    	void initArch();

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
        Thread *getThread(const Process *process, u16 tid) const;

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
		void killProcess(const Process *process);

		/**
		 *  Add a thread to the queue.
		 *
    	*  @param isUser Indicates whether it is a user-space thread.
	     *  @param rip The instruction pointer for the new thread.
		 *  @param process The parent process of the thread.
		 *
		 *  @return The new thread entry.
		 **/
		ThreadListEntry *addThread(bool isUser, u64 rip, Process *process);

		/**
		 * Terminate the specified thread.
		 *
		 * @param thread A pointer to the thread to be terminated.
		 **/
		void killThread(Thread *thread);

    	/**
		 * Terminate the specified thread.
		 *
		 * @param thread A pointer to the thread entry to be terminated.
		 **/
    	void killThread(const ThreadListEntry *thread);

		/**
		 *  Puts the specified thread to sleep for a given number of ticks.
		 *
		 *  @param thread The thread to be put to sleep.
		 *  @param ns The number of ticks for which the thread should remain asleep.
		 **/
		void sleepThread(Thread *thread, u64 ns) const;

		/**
		 *  Create a new context for a thread with the specified parameters.
		 *
		 *  @param isUser Indicates whether the context is for a user-space thread.
		 *  @param rip The instruction pointer for the new context.
		 *
		 *  @return The address of the created context.
		 */
		u64 *createContext(bool isUser, u64 rip);

    	static u32 sleepTick(u64 *);

    	TicketSpinLock *getSchedLock();

    private:
    	ExecutionNode *getCurrentExecutionNode() const;
    	u64 *createContextArch(bool isUser, u64 rip, u64 rsp);

    	TicketSpinLock schedLock {};

    public:
    	ProcessListEntry *processList {};

    	ThreadListEntry *readyThreadList {};

    	ThreadListEntry *queues[ProcessPriority::COUNT] {};
    	ThreadListEntry *lastQueueEntry[ProcessPriority::COUNT] {};

    	SleepQueueEntry *sleepQueue {};
    };
}

#endif