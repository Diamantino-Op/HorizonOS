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

	void Thread::setContext(u64 *context) {
		this->context = context;
	}

	u64 *Thread::getContext() const {
		return this->context;
	}

	void Thread::setSleepTicks(const u64 ticks) {
		this->sleepTicks = ticks;
	}

	u64 Thread::getSleepTicks() const {
		return this->sleepTicks;
	}

	void Thread::setState(const ThreadState state) {
		this->state = state;
	}

	ThreadState Thread::getState() {
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

		while (this->threadList != nullptr) {
			const ThreadListEntry *tmpEntry = this->threadList;
			this->threadList = this->threadList->nextProc;

			CommonMain::getInstance()->getScheduler()->killThread(tmpEntry);
		}

		VirtualAllocator::destroyContext(this->processContext);
	}

	void Process::setPriority(const ProcessPriority priority) {
		this->priority = priority;
	}

	ProcessPriority Process::getPriority() const {
		return this->priority;
	}

	AllocContext *Process::getProcessContext() const {
		return this->processContext;
	}

	void Process::addThread(ThreadListEntry *entry) {
		if (this->threadList != nullptr) {
			entry->prevProc = this->lastThreadList;
			this->lastThreadList->nextProc = entry;
			this->lastThreadList = entry;
		} else {
			this->threadList = entry;
			this->lastThreadList = entry;
		}
	}

	u16 Process::getId() const {
		return this->id;
	}

	// Execution Node

	void ExecutionNode::init() {
		Scheduler *schedulerPtr = CommonMain::getInstance()->getScheduler();

		//schedulerPtr->getSchedLock()->lock();

		auto *newThread = new Thread(CommonMain::getInstance()->getScheduler()->getProcess(0), CommonMain::getInstance()->getScheduler()->createContext(false, reinterpret_cast<u64>(idleThread)));

		newThread->setState(ThreadState::RUNNING);

		if (this->currentThread != nullptr) {
			auto *newThreadEntry = new ThreadListEntry();

			newThreadEntry->thread = newThread;

			if (ThreadListEntry *queueEntry = schedulerPtr->queues[ProcessPriority::LOW]; queueEntry != nullptr) {
				newThreadEntry->next = queueEntry;
				queueEntry->prev = newThreadEntry;
			}

			schedulerPtr->queues[ProcessPriority::LOW] = newThreadEntry;

			CommonMain::getInstance()->getScheduler()->getProcess(0)->addThread(newThreadEntry);
		} else {
			this->currentThread = new ThreadListEntry();

			this->currentThread->thread = newThread;

			CommonMain::getInstance()->getScheduler()->getProcess(0)->addThread(this->currentThread);
		}

		//schedulerPtr->getSchedLock()->unlock();
	}

	void ExecutionNode::setCurrentThread(ThreadListEntry *thread) {
		this->currentThread = thread;
	}

	ThreadListEntry *ExecutionNode::getCurrentThread() const {
		return this->currentThread;
	}

	void ExecutionNode::setRemainingTicks(const u8 remainingTicks) {
		this->remainingTicks = remainingTicks;
	}

	bool ExecutionNode::isDisabled() const {
		return this->isDisabledFlag;
	}

	void ExecutionNode::setDisabled(const bool val) {
		this->isDisabledFlag = val;
	}

	// Scheduler

	Scheduler::Scheduler() {
		this->processList = new ProcessListEntry();

		this->processList->process = new Process(ProcessPriority::LOW, CommonMain::getInstance()->getKernelAllocContext(), false);
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

	void Scheduler::addProcess(Process *process) {
		const auto newEntry = new ProcessListEntry();

		newEntry->process = process;

		newEntry->next = this->processList;

		this->processList = newEntry;
	}

	void Scheduler::killProcess(const Process *process) {
		delete process;
	}

	ThreadListEntry *Scheduler::addThread(const bool isUser, const u64 rip, Process *process) {
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

	void Scheduler::sleepThread(Thread *thread, const u64 ticks) const {
		thread->setSleepTicks(ticks);

		thread->setState(ThreadState::BLOCKED);

		this->getCurrentExecutionNode()->schedule();
	}

	u64 *Scheduler::createContext(const bool isUser, const u64 rip) {
		const auto newRsp = reinterpret_cast<u64>(malloc(threadCtxStackSize)) + threadCtxStackSize; // TODO: Maybe use process alloc context

		return createContextArch(isUser, rip, newRsp);
	}

	TicketSpinLock *Scheduler::getSchedLock() {
		return &this->schedLock;
	}
}