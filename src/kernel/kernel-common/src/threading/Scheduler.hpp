#ifndef KERNEL_COMMON_SCHEDULER_HPP
#define KERNEL_COMMON_SCHEDULER_HPP

#include "Types.hpp"
#include "LinkedList.hpp"
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
	class Scheduler;

    class Thread {
    public:
		explicit Thread(Process* parent, u64 *context);
    	explicit Thread(Scheduler *scheduler, Process* parent, u64 rip, bool isUser);
        ~Thread();

        void setContext(u64 *newContext);
        u64 *getContext() const;

        void setSleepNs(u64 ns);
        u64 getSleepNs() const;

    	void setStackPointer(u64 newStackPointer);
    	u64 *getStackPointer();

        void setState(ThreadState newState);
        ThreadState getState() const;

		u16 getId() const;

        Process *getParent() const;

    private:
        Process *parent {};
        u16 id {};

        u64 sleepNs {};

        u64 *context {};

		u64 stackPointer {};

        ThreadState state {};
    };

    class Process {
    public:
		explicit Process(ProcessPriority priority, bool isUserspace);

    	/**
		 *  Process constructor for processes that are owned ONLY by the kernel.
		 *
		 *  @param priority The process priority.
		 *  @param context The kernel alloc context.
		 **/
    	explicit Process(ProcessPriority priority, AllocContext *context);
        ~Process();

        void setPriority(ProcessPriority newPriority);
        ProcessPriority getPriority() const;

    	AllocContext *getProcessContext() const;

    	AllocContext *getProcessContextKernel() const;

    	LinkedListEntry<Thread> *addThread(Thread *entry);
    	void removeThread(Thread *entry);

    	u16 getId() const;

    private:
        u16 id {};

    	bool isUserspace {};

		LinkedListEntry<Thread> *mainThread {};

		LinkedList<Thread> threadList {};

        AllocContext *processContext {};
    	AllocContext *processContextKernel {};

        ProcessPriority priority {};
    };

    class ExecutionNode {
    public:
        ExecutionNode() = default;
        ~ExecutionNode() = default;

    	void init();

    	static void reSchedule();

    	void setCurrentThread(LinkedListEntry<Thread> *thread);
    	LinkedListEntry<Thread> *getCurrentThread() const;

		void switchThreads();

    	void switchContext(Thread *oldThread, Thread *newThread) const;
    	void switchContextMid(const Thread *newThread) const;

    	bool isDisabled() const;
    	void setDisabled(bool val);

    private:
    	static u32 scheduleTick(u64 *);

    	void schedule();

    	void initArch();

		bool isDisabledFlag {};

    	bool prevIF {};

        LinkedListEntry<Thread> *currentThread {};
    };

	void idleThread();

	[[noreturn]] void reaperFunction();

	extern "C" void switchContextAsm(u64 *oldStackPointer, u64 *newStackPointer, u64 newTableAddr, Thread *newThread);

	extern "C" void switchContextMidAsm(const Thread *newThread);

	constexpr u64 threadCtxStackSize = pageSize * 4;

	enum SchedulerFail {
		THREAD_ALREADY_SLEEPING,
		THREAD_ALREADY_BLOCKED,
		THREAD_NOT_FOUND
	};

	// TODO: Maybe do sleep queues and block queues
    class Scheduler {
    public:
        Scheduler();
        ~Scheduler() = default;

    	void initArch();

    	void startProcess(u64 startAddr, ProcessPriority priority, bool isUserspace);

    	bool startElfProcess(u64 *elfFile, ProcessPriority priority, bool isUserspace);

        /**
         *  Get the process with the specified PID.
         *
         *  @param pid The process ID.
         **/
        Process *getProcess(u16 pid);

        /**
         *  Get the thread with the specified TID.
         *
         *  @param process The process where the thread resides.
         *  @param tid The thread ID.
         **/
        Thread *getThread(const Process *process, u16 tid);

    	/**
		 *  Get the thread with the specified TID.
		 *
		 *  @param tid The thread ID.
		 **/
    	Thread *getThread(u16 tid);

		/**
		 *  Add a new process to the scheduler.
		 *
		 *  @param process A pointer to the process object to be added.
		 *
		 *  @return A pointer to the list entry of the thread.
		 **/
		LinkedListEntry<Process> *addProcess(Process *process);

		/**
		 * Terminate the specified process.
		 *
		 * @param process A pointer to the process to be terminated.
		 **/
		void killProcess(Process *process);

		/**
		 *  Add a thread to the queue.
		 *
    	 *  @param isUser Indicates whether it is a user-space thread.
	     *  @param rip The instruction pointer for the new thread.
		 *  @param process The parent process of the thread.
		 *
		 *  @return The new thread entry.
		 **/
		LinkedListEntry<Thread> *addThread(bool isUser, u64 rip, Process *process);

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
    	void killThread(const LinkedListEntry<Thread> *thread);

    	/**
		 * Removes the specified thread and frees the memory.
		 *
		 * @param thread A pointer to the thread entry to be removed.
		 **/
    	void removeThread(Thread *thread);

		/**
		 *  Puts the specified thread to sleep for a given number of ticks.
		 *
		 *  @param threadId The id of the thread to be put to sleep.
		 *  @param ns The number of ticks for which the thread should remain asleep.
		 **/
		void sleepThread(u16 threadId, u64 ns);

    	/**
		 *  Puts the specified thread to sleep for a given number of ticks.
		 *
		 *  @param thread The thread to be put to sleep.
		 *  @param ns The number of ticks for which the thread should remain asleep.
		 **/
    	void sleepThread(Thread *thread, u64 ns);

    	/**
		 *  Blocks the specified thread until it's unlocked manually.
		 *
		 *  @param threadId The id of the thread to be put to block.
		 **/
    	void blockThread(u16 threadId);

    	/**
		 *  Unblock the specified thread.
		 *
		 *  @param threadId The id of the thread to be put to unblock.
		 *  @param top Push the thread to the top of the queue.
		 **/
    	void unblockThread(u16 threadId, bool top);

		/**
		 *  Create a new context for a thread with the specified parameters.
		 *
		 *  @param isUser Indicates whether the context is for a user-space thread.
		 *  @param rip The instruction pointer for the new context.
		 *
		 *  @return The address of the created context.
		 */
		u64 *createContext(Thread *thread, Process *process, bool isUser, u64 rip);

		static void sendSleepEOI();

    	static u32 sleepTick(u64 *);

    	TicketSpinLock *getSchedLock();

    	static Thread *getCurrentThread();

    	static ExecutionNode *getCurrentExecutionNode();

    private:

    	TicketSpinLock schedLock {};

    public:
		LinkedList<Process> processList {};

    	LinkedList<Thread> queues[ProcessPriority::COUNT] {};

		LinkedList<Thread> readyThreadList {};
    	LinkedList<Thread> blockedThreadList {};
    	LinkedList<Thread> sleepingThreadList {};

    	LinkedList<Thread> awaitingKillThreadList {};
    };
}

#endif