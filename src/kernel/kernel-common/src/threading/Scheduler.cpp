#include "Scheduler.hpp"

#include "CommonMain.hpp"
#include "IDAllocator.hpp"
#include "memory/MainMemory.hpp"

namespace kernel::common::threading {
	// Threads

	Thread::Thread(Process* parent, u64 *context) : parent(parent), context(context) {
		this->id = TIDAllocator::allocTID();
	}

	Thread::~Thread() {
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

		this->processContext = VirtualAllocator::createContext(isUserspace, true);

		VirtualAllocator::shareKernelPages(this->processContext);
	}

	Process::Process(const ProcessPriority priority, AllocContext *context, const bool isUserspace) : isUserspace(isUserspace), processContext(context), priority(priority) {
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

	LinkedListEntry<Thread> *Process::addThread(Thread *entry) {
		return this->threadList.addStart(entry);
	}

	u16 Process::getId() const {
		return this->id;
	}

	// Execution Node

	void ExecutionNode::init() {
		Scheduler *schedulerPtr = CommonMain::getInstance()->getScheduler();

		auto *newThread = new Thread(schedulerPtr->getProcess(0), schedulerPtr->createContext(false, reinterpret_cast<u64>(idleThread)));

		newThread->setState(ThreadState::RUNNING);

		this->currentThread = schedulerPtr->getProcess(0)->addThread(newThread);

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

	// Scheduler

	Scheduler::Scheduler() {
		this->processList.addStart(new Process(ProcessPriority::LOW, CommonMain::getInstance()->getKernelAllocContext(), false));
	}

	Process *Scheduler::getProcess(const u16 pid) const {
		auto currEntry = this->processList;

		while (currEntry != nullptr) {
			if (currEntry->process->getId() == pid) {
				return currEntry->process;
			}

			currEntry = currEntry->next;
		}

		return nullptr;
	}

	Thread *Scheduler::getThread(const Process *process, const u16 tid) const {
		auto currEntry = this->queues[process->getPriority()];

		while (currEntry != nullptr) {
			if (currEntry->thread->getId() == tid) {
				return currEntry->thread;
			}

			currEntry = currEntry->next;
		}

		return nullptr;
	}

	LinkedListEntry<Thread> *Scheduler::getThread(const u16 tid) const {
		for (ThreadListEntry* currQueue : this->queues) {
			ThreadListEntry *currEntry = currQueue;

			while (currEntry != nullptr) {
				if (currEntry->thread->getId() == tid) {
					return currEntry;
				}

				currEntry = currEntry->next;
			}
		}

		ThreadListEntry *currEntry = this->sleepingThreadList;

		while (currEntry != nullptr) {
			if (currEntry->thread->getId() == tid) {
				return currEntry;
			}

			currEntry = currEntry->next;
		}

		currEntry = this->blockedThreadList;

		while (currEntry != nullptr) {
			if (currEntry->thread->getId() == tid) {
				return currEntry;
			}

			currEntry = currEntry->next;
		}

		return nullptr;
	}

	LinkedListEntry<Thread> *Scheduler::addProcess(Process *process) {
		const auto newEntry = new ProcessListEntry();

		newEntry->process = process;

		newEntry->next = this->processList;

		this->processList = newEntry;
	}

	void Scheduler::killProcess(const Process *process) {
		delete process;
	}

	LinkedListEntry<Thread> *Scheduler::addThread(const bool isUser, const u64 rip, Process *process) {
		auto *newThread = new Thread(process, createContext(isUser, rip));

		newThread->setState(ThreadState::READY);

		auto *newThreadEntry = new ThreadListEntry();

		newThreadEntry->thread = newThread;

		newThreadEntry->next = this->readyThreadList;
		this->readyThreadList = newThreadEntry;

		process->addThread(newThreadEntry);

		return newThreadEntry;
	}

	void Scheduler::killThread(Thread *thread) {
		thread->setState(ThreadState::TERMINATED);

		if (this->getCurrentExecutionNode()->getCurrentThread()->thread == thread) {
			this->getCurrentExecutionNode()->schedule();
		}

		const ThreadListEntry *selectedEntry = this->queues[thread->getParent()->getPriority()];

		while (selectedEntry != nullptr) {
			if (selectedEntry->thread == thread) {
				break;
			}

			selectedEntry = selectedEntry->next;
		}

		killThread(selectedEntry);
	}

	void Scheduler::killThread(const ThreadListEntry *thread) {
		if (this->queues[thread->thread->getParent()->getPriority()] == thread) {
			this->queues[thread->thread->getParent()->getPriority()] = thread->next;
		}

		if (thread->prev != nullptr) {
			thread->prev->next = thread->next;
		}

		if (thread->prevProc != nullptr) {
			thread->prevProc->nextProc = thread->nextProc;
		}

		if (thread->next != nullptr) {
			thread->next->prev = thread->prev;
		}

		if (thread->nextProc != nullptr) {
			thread->nextProc->prevProc = thread->prevProc;
		}

		delete thread->thread;
		delete thread;
	}

	void Scheduler::sleepThread(const u16 threadId, const u64 ns) {
		ThreadListEntry *currThreadEntry = this->getThread(threadId);

		currThreadEntry->thread->setSleepNs(CommonMain::getInstance()->getClocks()->getMainClock()->getNs() + ns);

		CommonMain::getTerminal()->debug("Sleep Ns: %llu for thread: %u", "Scheduler", currThreadEntry->thread->getSleepNs(), currThreadEntry->thread->getId());

		currThreadEntry->thread->setState(ThreadState::BLOCKED);

		if (currThreadEntry == this->lastQueueEntry[currThreadEntry->thread->getParent()->getPriority()]) {
			this->lastQueueEntry[currThreadEntry->thread->getParent()->getPriority()] = currThreadEntry->prev;
		}

		if (currThreadEntry == this->queues[currThreadEntry->thread->getParent()->getPriority()]) {
			this->queues[currThreadEntry->thread->getParent()->getPriority()] = currThreadEntry->next;
		}

		if (currThreadEntry->next != nullptr) {
			currThreadEntry->next->prev = currThreadEntry->prev;
		}

		if (currThreadEntry->prev != nullptr) {
			currThreadEntry->prev->next = currThreadEntry->next;
		}

		this->sleepingThreadList->prev = currThreadEntry;

		currThreadEntry->next = nullptr;
		currThreadEntry->next = this->sleepingThreadList;

		this->getCurrentExecutionNode()->schedule();
	}

	void Scheduler::blockThread(u16 threadId) const {
		const ThreadListEntry *currThreadEntry = this->getThread(threadId);

		CommonMain::getTerminal()->debug("Blocking thread: thread: %u", "Scheduler", thread->getId());

		thread->setState(ThreadState::BLOCKED);



		this->getCurrentExecutionNode()->schedule();
	}

	void Scheduler::unblockThread(u16 threadId) const {
		const ThreadListEntry *currThreadEntry = this->getThread(threadId);

		CommonMain::getTerminal()->debug("Unblocking thread: thread: %u", "Scheduler", thread->getId());

		if (thread->getSleepNs() <= CommonMain::getInstance()->getClocks()->getMainClock()->getNs()) {
			thread->setState(ThreadState::RUNNING);
		}


	}

	u64 *Scheduler::createContext(const bool isUser, const u64 rip) {
		const auto newRsp = reinterpret_cast<u64>(malloc(threadCtxStackSize)) + threadCtxStackSize; // TODO: Maybe use process alloc context

		return createContextArch(isUser, rip, newRsp);
	}

	u32 Scheduler::sleepTick(u64 *) {
		Scheduler *schedulerPtr = CommonMain::getInstance()->getScheduler();

		const bool prevIF = schedulerPtr->getSchedLock()->lock();

		ThreadListEntry *tmpEntry = schedulerPtr->sleepingThreadList;

		while (tmpEntry != nullptr) {
			if (tmpEntry->thread->getSleepNs() > 0) {
				if (tmpEntry->thread->getSleepNs() <= CommonMain::getInstance()->getClocks()->getMainClock()->getNs()) {
					tmpEntry->thread->setState(ThreadState::RUNNING);

					tmpEntry->thread->setSleepNs(0);

					if (tmpEntry->prev != nullptr) {
						tmpEntry->prev->next = tmpEntry->next;
					}

					if (tmpEntry->next != nullptr) {
						tmpEntry->next->prev = tmpEntry->prev;
					}

					schedulerPtr->lastQueueEntry[tmpEntry->thread->getParent()->getPriority()]->next = tmpEntry;

					tmpEntry->prev = schedulerPtr->lastQueueEntry[tmpEntry->thread->getParent()->getPriority()];
					tmpEntry->next = nullptr;

					schedulerPtr->lastQueueEntry[tmpEntry->thread->getParent()->getPriority()] = tmpEntry;
				}
			}

			tmpEntry = tmpEntry->next;
		}

		schedulerPtr->getSchedLock()->unlock(prevIF);

		return 0;
	}

	TicketSpinLock *Scheduler::getSchedLock() {
		return &this->schedLock;
	}
}