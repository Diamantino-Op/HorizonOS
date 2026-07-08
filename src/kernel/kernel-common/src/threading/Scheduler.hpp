#ifndef KERNEL_COMMON_SCHEDULER_HPP
#define KERNEL_COMMON_SCHEDULER_HPP

#include "IDAllocator.hpp"
#include "LinkedList.hpp"
#include "Types.hpp"
#include "memory/VirtualAllocator.hpp"
#include "bstree.hpp"

namespace kernel::common::threading {
    using namespace memory;

    constexpr u8 maxTicks = 50; // 50ms with PIT at 1kHz
	constexpr usize signalActionCount = 64;
	constexpr usize signalMaskWordCount = 16;

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

	enum ThreadOS {
		HORIZONOS = 0,
		LINUX = 1,
		WINDOWS = 2,
		MACOS = 3
	};

    struct SignalAction {
        u64 handler {};
        u64 flags {};
        u64 restorer {};
        u64 mask[signalMaskWordCount] {};
    };

	struct SignalContext {
	    u64 rax {};
	    u64 rbx {};
	    u64 rcx {};
	    u64 rdx {};
	    u64 rsi {};
	    u64 rdi {};
	    u64 r8 {};
	    u64 r9 {};
	    u64 r10 {};
	    u64 r11 {};
	    u64 r12 {};
	    u64 r13 {};
	    u64 r14 {};
	    u64 r15 {};
	    u64 rip {};
	    u64 rFlags {};
	    u64 rsp {};
	    u64 cs {};
	    u64 ss {};
    };

    class Process;
	class Scheduler;
	class ExecutionNode;

    class Thread {
    public:
		explicit Thread(Process* parent, u64 *context);
    	explicit Thread(Scheduler *scheduler, Process* parent, u64 rip, bool isUser, u64 rsp = 0, u64 userRsp = 0, bool is32Bit = false, ThreadOS os = ThreadOS::HORIZONOS);
        ~Thread();

    	void deleteThreadArch() const;

        void setContext(u64 *newContext);
        u64 *getContext() const;

        void setSleepNs(u64 ns);
        u64 getSleepNs() const;

        void setWaitingPort(u64 port);
        u64 getWaitingPort() const;

    	void queueSignal(u64 signal);
    	bool hasPendingSignal() const;
    	u64 getPendingSignal() const;
    	bool hasSignalFrame() const;
    	void setSignalFrame(const SignalContext &frame);
    	const SignalContext &getSignalFrame() const;
    	void clearPendingSignal();
    	void clearSignalFrame();
    	void clearSignalState();

    	void setStackPointer(u64 newStackPointer);
    	u64 *getStackPointer();

    	void setKStackPointer(u64 newKStackPointer);
    	u64 getKStackPointer() const;
    	void setKernelStackOwned(bool owned);

    	void setSyscallStackPointer(u64 newSyscallStackPointer);
    	u64 getSyscallStackPointer() const;

    	bool is32Bit() const;

    	ThreadOS getOS() const;

        void setState(ThreadState newState);
        ThreadState getState() const;

		u16 getId() const;
		u64 getGeneration() const;

        Process *getParent() const;

    	u64 getLockedCoreId() const;
    	void setLockedCoreId(u64 newId);

    	void setPendingWakeup(bool val);
    	bool getPendingWakeup() const;

    	void setQueuedExecutionNode(ExecutionNode *node);
    	ExecutionNode *getQueuedExecutionNode() const;

    	void setQueued(bool val);
    	bool isQueued() const;

    	u8 computeInteractiveScore() const;

    	void recomputeDynPriority();

    private:
        Process *parent {};
        u16 id {};
		u64 generation {};

        u64 sleepNs {};
        u64 waitingPort {};

    	bool signalPending {};
    	u64 pendingSignal {};
    	bool signalFrameValid {};
    	SignalContext signalFrame {};

        u64 *context {};

		u64 stackPointer {};

    	u64 kernelStackPointer {};
    	bool kernelStackOwned {};

    	u64 syscallStackPointer {};

    	bool bit32 {};

    	ThreadOS os {};

        ThreadState state {};

    	u64 lockedCoreId {};

    	bool pendingWakeup {};

    	ExecutionNode *queuedExecutionNode {};
    	bool queued {};

    public:
    	u64 runTime     {};   // ns spent running (updated on context switch-out)
    	u64 sleepTime   {};   // ns spent sleeping (updated on wakeup)
    	u8  dynPriority {}; // 0=highest, 255=lowest; recomputed each tick

    	u64 lastScheduledNs {};

    	bstree_node_t bstNode {};   // embedded node — no separate allocation needed
    	u64           schedKey  {};  // (dynPriority << 32) | insertionSeq, set on enqueue
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
		bool ownsProcessContext() const;
		bool isTerminating() const;
		void setTerminating(bool val);

		LinkedListEntry<Thread> *addThread(Thread *entry);
    	void removeThread(Thread *entry);

    	u16 getId() const;

    private:
        u16 id {};

    	bool isUserspace {};

        AllocContext *processContext {};
		AllocContext *processContextKernel {};
		bool processContextOwned {};
		bool terminating {};

        ProcessPriority priority {};

    public:
    	LinkedList<Thread> threadList {};

    	PRIDAllocator pridAllocator {};

    	u64 topmostMappedPage {};
        SignalAction signalActions[signalActionCount] {};
    };

	struct UleRunQueue {
		bstree_t tree {};
		u64      seqCounter {}; // monotonic insertion counter for FIFO within same priority
		u8       minDynPrio {}; // tracks the best (lowest) dynPriority currently in tree
		bool     valid      {}; // false when tree is empty
		ExecutionNode *owner {};

		// value_of_node callback — must be a free function or static lambda
		static auto nodeKey(bstree_node_t* n) -> u64 {
			const Thread *thread = container_of(n, &Thread::bstNode);

			return thread->schedKey;
		}

		void init(ExecutionNode *newOwner) {
			this->owner              = newOwner;
			this->tree.value_of_node = &UleRunQueue::nodeKey;
			this->tree.root          = nullptr;
			this->tree.type          = BST_TYPE_RB; // balanced — O(log n) insert/remove
			this->valid              = false;
		}

		void enqueue(Thread* thread, const u8 dynPrio) {
			thread->dynPriority = dynPrio;
			thread->schedKey    = (static_cast<u64>(dynPrio) << 32) | (this->seqCounter++);
			thread->setQueuedExecutionNode(this->owner);
			thread->setQueued(true);

			bstree_insert(&this->tree, &thread->bstNode);

			if (not this->valid or dynPrio < this->minDynPrio) {
				this->minDynPrio = dynPrio;
				this->valid      = true;
			}
		}

		void enqueueWaking(Thread *thread, const u8 rawDynPrio) {
			u8 clamped = rawDynPrio;

			if (this->valid) {
				// Don't let a waking thread be better than
				// (best current thread - 1 priority step), minimum 0
				const u8 floor = this->minDynPrio > 0 ? this->minDynPrio - 1 : 0;

				if (clamped < floor) {
					clamped = floor;
				}
			}

			enqueue(thread, clamped);
		}

		// O(1): leftmost node = smallest key = highest priority
		auto dequeueMin() -> Thread* {
			if (this->tree.root == nullptr) {
				this->valid = false; return nullptr;
			}

			bstree_node_t *min = bstree_minimum(this->tree.root);
			bstree_remove(&this->tree, min);
			Thread *thread = container_of(min, &Thread::bstNode);
			thread->setQueued(false);
			thread->setQueuedExecutionNode(nullptr);
			thread->bstNode = {};

			// Recompute minDynPrio from new minimum
			if (this->tree.root != nullptr) {
				bstree_node_t *newMin = bstree_minimum(this->tree.root);
				const Thread *newThread = container_of(newMin, &Thread::bstNode);

				this->minDynPrio = newThread->dynPriority;
			} else {
				this->valid = false;
			}

			return thread;
		}

		auto remove(Thread* thread) -> bool {
			if (!thread->isQueued() || thread->getQueuedExecutionNode() != this->owner) {
				return false;
			}

			bstree_remove(&tree, &thread->bstNode);
			thread->setQueued(false);
			thread->setQueuedExecutionNode(nullptr);
			thread->bstNode = {};

			// Recompute minDynPrio after arbitrary removal
			if (this->tree.root != nullptr) {
				bstree_node_t *newMin = bstree_minimum(tree.root);
				const Thread *tMin = container_of(newMin, &Thread::bstNode);

				minDynPrio = tMin->dynPriority;
				valid      = true;
			} else {
				valid = false;
			}

			return true;
		}

		auto stealOne() -> Thread * {
			if (tree.root == nullptr) {
				return nullptr;
			}

			bstree_node_t *max = bstree_maximum(tree.root);
			bstree_remove(&tree, max);
			Thread *thread = container_of(max, &Thread::bstNode);
			thread->setQueued(false);
			thread->setQueuedExecutionNode(nullptr);
			thread->bstNode = {};

			if (tree.root != nullptr) {
				bstree_node_t *newMin = bstree_minimum(tree.root);
				const Thread *newThread = container_of(newMin, &Thread::bstNode);

				minDynPrio = newThread->dynPriority;
				valid      = true;
			} else {
				valid = false;
			}

			return thread;
		}

		auto size() const -> usize {
			usize count = 0;

			if (tree.root == nullptr) {
				return 0;
			}

			const bstree_node_t *cur = bstree_minimum(tree.root);

			while (cur != nullptr) {
				count++;

				cur = bstree_successor(cur);
			}

			return count;
		}

		auto isEmpty() const -> bool {
			return tree.root == nullptr;
		}
	};

    class ExecutionNode {
    public:
        ExecutionNode();
        ~ExecutionNode() = default;

    	void init();

    	static void reSchedule();

    	u128 schedule(u64 oldRsp);

    	void setCurrentThread(Thread *thread);
    	Thread *getCurrentThread() const;
    	Thread *getIdleThread() const;

    	Thread* getNextThread();
    	void enqueueThread(Thread *thread, bool waking = false);
    	bool removeThread(Thread *thread);
    	bool hasRunnableThreads() const;

		u128 saveOldThread(u64 oldRsp);

    	void loadNewThread();

    	bool isDisabled() const;
    	void setDisabled(bool val);

    	u64 getENThreadRsp() const;

        void setPendingSchedUnlock(bool prevIF);
        bool hasPendingSchedUnlock() const;
        bool consumePendingSchedUnlock();
        void finishScheduleSwitch();

    private:
		bool isDisabledFlag {};
        bool pendingSchedUnlock {};
        bool pendingSchedUnlockIF {};

        Thread *idleThread {};
        Thread *currentThread {};

    	bool oldThreadWasIopb {};
    	bool idleThreadStarted {};

    	bool isScheduling {};

    	//u64 prevPrevThreadId {};
    	//u64 prevPrevCount {};

    public:
    	UleRunQueue uleQueue {};
    	TicketSpinLock coreLock {};
    };

	[[noreturn]] void idleThreadFun();

	[[noreturn]] void reaperFunction();

	extern "C" void switchContextAsm();

	// TODO: Make configurable
    constexpr u64 threadCtxStackSize = pageSize * 2;
	constexpr u64 threadUserStackSize = pageSize * 8;

	// TODO: Maybe do sleep queues and block queues
    class Scheduler {
    public:
        Scheduler();
        ~Scheduler() = default;

    	static void initArch();

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
        bool removeThread(Thread *thread);

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
    	void blockThread(u16 threadId, bool useLock = true);

    	/**
		 *  Unblock the specified thread.
		 *
		 *  @param threadId The id of the thread to be put to unblock.
		 *  @param top Push the thread to the top of the queue.
		 **/
		void unblockThread(u16 threadId, bool top, bool useLock = true);
		void unblockThreadIfWaiting(u16 threadId, u64 generation, u64 port, bool top, bool useLock = true);

		void enqueueThread(Thread *thread, bool waking = false);
    	void enqueueThread(Thread *thread, ExecutionNode *target, bool waking = false);
    	auto hasRunnableThreads() -> bool;

		/**
		 *  Create a new context for a thread with the specified parameters.
		 *
		 *  @param isUser Indicates whether the context is for a user-space thread.
		 *  @param rip The instruction pointer for the new context.
		 *
		 *  @return The address of the created context.
		 */
		auto createContext(Thread *thread, Process *process, bool isUser, u64 rip, u64 rsp = 0, u64 userRsp = 0) -> u64 *;

    	static auto getCoreEN(u64 cpuId) -> ExecutionNode *;

    	static void sleepTick();

    	static void timerReSchedule();
    	static auto intReSchedule(u64 *) -> u32;

    	auto getSchedLock() -> TicketSpinLock *;

    	static auto getCurrentThread() -> Thread *;

    	static auto getCurrentExecutionNode() -> ExecutionNode *;

    	void reaperThreadArch(const LinkedListEntry<Thread> *thread);
    	void reaperProcessArch(Process *process);

    	static void debugDump();

    private:
    	TicketSpinLock schedLock {};

    public:
    	static bool isDisabled;

		LinkedList<Process> processList {};

    	LinkedList<Thread> blockedThreadList {};
    	LinkedList<Thread> sleepingThreadList {};

    	LinkedList<Thread> awaitingKillThreadList {};
    };
}

#endif

