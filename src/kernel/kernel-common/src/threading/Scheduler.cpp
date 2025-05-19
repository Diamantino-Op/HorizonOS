#include "Scheduler.hpp"

#include "CommonMain.hpp"
#include "IDAllocator.hpp"
#include "memory/MainMemory.hpp"

namespace kernel::common::threading {
	// Threads

	Thread::Thread(Process* parent, u64 *context) : parent(parent), context(context) {}

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

		this->processContext = VirtualAllocator::createContext(isUserspace);
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

	void Process::addThread(ThreadListEntry *entry) {
		entry->prevProc = this->lastThreadList;
		this->lastThreadList->nextProc = entry;
		this->lastThreadList = entry;
	}

	u16 Process::getId() const {
		return this->id;
	}

	// Execution Node

	ExecutionNode::ExecutionNode() {
		CommonMain::getInstance()->getScheduler()->addThread(false, reinterpret_cast<u64>(idleThread), CommonMain::getInstance()->getScheduler()->getProcess(0));
	}

	void ExecutionNode::setCurrentThread(ThreadListEntry *thread) {
		this->currentThread = thread;
	}

	ThreadListEntry *ExecutionNode::getCurrentThread() const {
		return this->currentThread;
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

	void Scheduler::addThread(const bool isUser, const u64 rip, Process *process) {
		auto *newThread = new Thread(process, createContext(isUser, rip));

		newThread->setState(ThreadState::READY);

		auto *newThreadEntry = new ThreadListEntry();

		newThreadEntry->thread = newThread;

		newThreadEntry->next = this->readyThreadList;
		this->readyThreadList = newThreadEntry;

		process->addThread(newThreadEntry);

		// TODO: Use this in schedule method
		/*newThreadEntry->prev = this->lastQueueEntry[process->getPriority()];
		this->lastQueueEntry[process->getPriority()]->next = newThreadEntry;
		this->lastQueueEntry[process->getPriority()] = newThreadEntry;*/
	}

	void Scheduler::killThread(Thread *thread) {
		for (u64 i = 0; i < this->executionNodesAmount; i++) {
			if (this->executionNodes[i].getCurrentThread()->thread == thread) {
				this->executionNodes[i].setCurrentThread(nullptr);
			}
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

	void Scheduler::sleepThread(Thread *thread, const u64 ticks) {
		thread->setSleepTicks(ticks);

		thread->setState(ThreadState::BLOCKED);
	}

	u64 *Scheduler::createContext(const bool isUser, const u64 rip) {
		const auto newRsp = reinterpret_cast<u64>(malloc(threadCtxStackSize)) + threadCtxStackSize; // TODO: Maybe use process alloc context

		return createContextArch(isUser, rip, newRsp);
	}
}