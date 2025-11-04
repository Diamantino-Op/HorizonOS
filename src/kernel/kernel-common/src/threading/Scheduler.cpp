#include "Scheduler.hpp"

#include "CommonMain.hpp"
#include "IDAllocator.hpp"
#include "memory/MainMemory.hpp"

namespace kernel::common::threading {
	// Threads

	Thread::Thread(Scheduler *scheduler, Process* parent, const u64 rip, const bool isUser) : parent(parent) {
		this->context = scheduler->createContext(this, parent, isUser, rip);

		this->id = TIDAllocator::allocTID();
	}

	Thread::Thread(Process* parent, u64 *context) : parent(parent), context(context) {
		this->id = TIDAllocator::allocTID();
	}

	Thread::~Thread() {
		TIDAllocator::freeTID(this->id);

		delete this->context;
	}

	void Thread::setContext(u64 *newContext) {
		this->context = newContext;
	}

	u64 *Thread::getContext() const {
		return this->context;
	}

	void Thread::setSleepNs(const u64 ns) {
		this->sleepNs = ns;
	}

	u64 Thread::getSleepNs() const {
		return this->sleepNs;
	}

	void Thread::setStackPointer(const u64 newStackPointer) {
		this->stackPointer = newStackPointer;
	}

	u64 *Thread::getStackPointer() {
		return &this->stackPointer;
	}

	void Thread::setState(const ThreadState newState) {
		this->state = newState;
	}

	ThreadState Thread::getState() const {
		return this->state;
	}

	u16 Thread::getId() const {
		return this->id;
	}

	Process *Thread::getParent() const {
		return this->parent;
	}

	// Process

	Process::Process(const ProcessPriority priority, const bool isUserspace) : isUserspace(isUserspace), priority(priority) {
		this->id = PIDAllocator::allocPID();

		CreatedContext createdContext;

		if (isUserspace) {
			createdContext = VirtualAllocator::createUserContext();
		} else {
			createdContext = VirtualAllocator::createContext(true);
		}

		this->processContext = createdContext.ctx;
		this->processContextKernel = createdContext.ctkKern;

		VirtualAllocator::shareKernelPages(this->processContext);

		VirtualAllocator::initContext(this->processContext);
	}

	Process::Process(const ProcessPriority priority, AllocContext *context) : processContext(context), processContextKernel(context), priority(priority) {
		this->id = PIDAllocator::allocPID();
	}

	Process::~Process() {
		PIDAllocator::freePID(this->id);

		const LinkedListEntry<Thread> *tmpEntry = this->threadList.getFirst();

		while (tmpEntry != nullptr) {
			const LinkedListEntry<Thread> *newTmpEntry = tmpEntry;
			tmpEntry = tmpEntry->next;

			CommonMain::getInstance()->getScheduler()->killThread(newTmpEntry);
		}

		VirtualAllocator::destroyContext(this->processContext);
	}

	void Process::setPriority(const ProcessPriority newPriority) {
		this->priority = newPriority;
	}

	ProcessPriority Process::getPriority() const {
		return this->priority;
	}

	AllocContext *Process::getProcessContext() const {
		return this->processContext;
	}

	AllocContext *Process::getProcessContextKernel() const {
		return this->processContextKernel;
	}

	LinkedListEntry<Thread> *Process::addThread(Thread *entry) {
		return this->threadList.addStart(entry);
	}

	void Process::removeThread(Thread *entry) {
		this->threadList.remove(entry, false);
	}

	u16 Process::getId() const {
		return this->id;
	}

	// Execution Node

	void ExecutionNode::init() {
		Scheduler *schedulerPtr = CommonMain::getInstance()->getScheduler();

		Process *idleProcess = schedulerPtr->getProcess(0);

		auto *newThread = new Thread(schedulerPtr, idleProcess, reinterpret_cast<u64>(idleThread), false);

		newThread->setState(ThreadState::RUNNING);

		schedulerPtr->getProcess(0)->addThread(newThread);

		this->currentThread = new LinkedListEntry<Thread>();

		this->currentThread->value = newThread;

		this->initArch();
	}

	void ExecutionNode::setCurrentThread(LinkedListEntry<Thread> *thread) {
		this->currentThread = thread;
	}

	LinkedListEntry<Thread> *ExecutionNode::getCurrentThread() const {
		return this->currentThread;
	}

	bool ExecutionNode::isDisabled() const {
		return this->isDisabledFlag;
	}

	void ExecutionNode::setDisabled(const bool val) {
		this->isDisabledFlag = val;
	}

	// Reaper Thread

	[[noreturn]] void reaperFunction() {
		Scheduler *scheduler = CommonMain::getInstance()->getScheduler();

		for (;;) {
			auto *currThread = Scheduler::getCurrentThread();

			if (scheduler->awaitingKillThreadList.getSize() > 0) {
				for (auto &currEntry : scheduler->awaitingKillThreadList) {
					scheduler->removeThread(&currEntry);
				}
			}

			scheduler->sleepThread(currThread, 500ull * 1'000'000ull); // TODO: ms to ns and vice versa function
		}
	}

	// Scheduler

	Scheduler::Scheduler() {
		this->addProcess(new Process(ProcessPriority::LOW, CommonMain::getInstance()->getKernelAllocContext()));

		auto *reaperProcess = new Process(ProcessPriority::VERY_HIGH, CommonMain::getInstance()->getKernelAllocContext());

		this->addProcess(reaperProcess);

		this->addThread(false, reinterpret_cast<u64>(reaperFunction), reaperProcess);
	}

	Process *Scheduler::getProcess(const u16 pid) {
		for (auto &currEntry : this->processList) {
			if (currEntry.getId() == pid) {
				return &currEntry;
			}
		}

		return nullptr;
	}

	Thread *Scheduler::getThread(const Process *process, const u16 tid) {
		for (auto &currEntry : this->queues[process->getPriority()]) {
			if (currEntry.getId() == tid) {
				return &currEntry;
			}
		}

		return nullptr;
	}

	Thread *Scheduler::getThread(const u16 tid) {
		for (LinkedList<Thread>& currQueue : this->queues) {
			for (auto &currEntry : currQueue) {
				if (currEntry.getId() == tid) {
					return &currEntry;
				}
			}
		}

		for (auto &currEntry : this->sleepingThreadList) {
			if (currEntry.getId() == tid) {
				return &currEntry;
			}
		}

		for (auto &currEntry : this->blockedThreadList) {
			if (currEntry.getId() == tid) {
				return &currEntry;
			}
		}

		return nullptr;
	}

	LinkedListEntry<Process> *Scheduler::addProcess(Process *process) {
		return this->processList.addStart(process);
	}

	void Scheduler::killProcess(Process *process) {
		this->processList.remove(process);
	}

	LinkedListEntry<Thread> *Scheduler::addThread(const bool isUser, const u64 rip, Process *process) {
		auto *newThread = new Thread(this, process, rip, isUser);

		newThread->setState(ThreadState::READY);

		process->addThread(newThread);

		// TODO: Remove
		if (isUser)
			return this->readyThreadList.addStart(newThread);

		return this->readyThreadList.addEnd(newThread);
	}

	void Scheduler::killThread(Thread *thread) {
		thread->setState(ThreadState::TERMINATED);

		if (this->getCurrentExecutionNode()->getCurrentThread()->value == thread) {
			ExecutionNode::reSchedule();
		} else {
			this->removeThread(thread);
		}
	}

	void Scheduler::killThread(const LinkedListEntry<Thread> *thread) {
		this->killThread(thread->value);
	}

	void Scheduler::removeThread(Thread *thread) {
		if (!this->queues[thread->getParent()->getPriority()].remove(thread)) {
			if (!this->sleepingThreadList.remove(thread)) {
				if (!this->blockedThreadList.remove(thread)) {
					this->readyThreadList.remove(thread);
				}
			}
		}
	}

	void Scheduler::sleepThread(const u16 threadId, const u64 ns) {
		this->sleepThread(this->getThread(threadId), ns);
	}

	void Scheduler::sleepThread(Thread *thread, const u64 ns) {
		thread->setSleepNs(CommonMain::getInstance()->getClocks()->getMainClock()->getNs() + ns);

		CommonMain::getTerminal()->debug("Sleep Ns: %llu for thread: %u", "Scheduler", thread->getSleepNs(), thread->getId());

		thread->setState(ThreadState::BLOCKED);

		this->queues[thread->getParent()->getPriority()].remove(thread);

		if (this->getCurrentExecutionNode()->getCurrentThread()->value == thread) {
			ExecutionNode::reSchedule();
		} else if (!this->blockedThreadList.contains(thread)) {
			this->sleepingThreadList.addStart(thread);
		}
	}

	void Scheduler::blockThread(const u16 threadId) {
		Thread *thread = this->getThread(threadId);

		CommonMain::getTerminal()->debug("Blocking thread: thread: %u", "Scheduler", thread->getId());

		thread->setState(ThreadState::BLOCKED);

		if (!this->queues[thread->getParent()->getPriority()].remove(thread, false) and thread->getSleepNs() > 0) {
			this->sleepingThreadList.remove(thread, false);
		}

		if (this->getCurrentExecutionNode()->getCurrentThread()->value == thread) {
			ExecutionNode::reSchedule();
		} else {
			this->blockedThreadList.addStart(thread);
		}
	}

	void Scheduler::unblockThread(const u16 threadId, const bool top) {
		Thread *thread = this->getThread(threadId);

		CommonMain::getTerminal()->debug("Unblocking thread: thread: %u", "Scheduler", thread->getId());

		this->blockedThreadList.remove(thread, false);

		if (thread->getSleepNs() == 0) {
			thread->setState(ThreadState::RUNNING);

			if (top) {
				this->queues[thread->getParent()->getPriority()].addStart(thread);
			} else {
				this->queues[thread->getParent()->getPriority()].addEnd(thread);
			}
		} else {
			this->sleepingThreadList.addStart(thread);
		}
	}

	u32 Scheduler::sleepTick(u64 *) {
		Scheduler *schedulerPtr = CommonMain::getInstance()->getScheduler();

		const bool prevIF = schedulerPtr->getSchedLock()->lock();

		for (auto &currEntry : schedulerPtr->sleepingThreadList) {
			if (currEntry.getSleepNs() > 0) {
				if (currEntry.getSleepNs() <= CommonMain::getInstance()->getClocks()->getMainClock()->getNs()) {
					currEntry.setState(ThreadState::RUNNING);

					currEntry.setSleepNs(0);

					schedulerPtr->sleepingThreadList.remove(&currEntry, false);

					schedulerPtr->queues[currEntry.getParent()->getPriority()].addEnd(&currEntry);
				}
			}
		}

		sendSleepEOI();

		schedulerPtr->getSchedLock()->unlock(prevIF);

		return 0;
	}

	TicketSpinLock *Scheduler::getSchedLock() {
		return &this->schedLock;
	}
}