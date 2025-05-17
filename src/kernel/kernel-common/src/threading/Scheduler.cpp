#include "Scheduler.hpp"

#include "IDAllocator.hpp"
#include "memory/MainMemory.hpp"

namespace kernel::common::threading {
	// Threads

	Thread::Thread(Process* parent) {

	}

	Thread::~Thread() {

	}

	void Thread::setContext(u64 *context) {
		this->context = context;
	}

	u64 *Thread::getContext() {
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

	Process *Thread::getParent() {
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

		while (this->threadList->nextProc != nullptr) {

		}

		VirtualAllocator::destroyContext(this->processContext);
	}

	void Process::setPriority(const ProcessPriority priority) {
		this->priority = priority;
	}

	ProcessPriority Process::getPriority() {
		return this->priority;
	}

	u16 Process::getId() const {
		return this->id;
	}

	// Execution Node

	ExecutionNode::ExecutionNode() {

	}

	ThreadListEntry *ExecutionNode::getCurrentThread() const {
		return this->currentThread;
	}

	// Scheduler

	Scheduler::Scheduler() {
		this->processList = new ProcessListEntry();

		this->processList->process = new Process(ProcessPriority::LOW);
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

	Thread *Scheduler::getThread(Process *process, const u16 tid) const {
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

	void Scheduler::killProcess(Process *process) {
		for (u64 i = 0; i < this->executionNodesAmount; i++) {
			if (this->executionNodes[i].getCurrentThread()->thread->getParent()->getId() == process->getId()) {

			}
		}
	}

	void Scheduler::addThread(Thread *thread, ProcessPriority priority) {

	}

	void Scheduler::killThread(Thread *thread) {

	}

	void Scheduler::killThread(ThreadListEntry *thread) {
		if (this->queues[thread->thread->getParent()->getPriority()] == thread) {
			this->queues[thread->thread->getParent()->getPriority()] = thread->next;
		}
	}

	void Scheduler::sleepThread(Thread *thread, u64 ticks) {

	}

	u64 *Scheduler::createContext(const bool isUser, const u64 rip) {
		const auto newRsp = reinterpret_cast<u64>(malloc(threadCtxStackSize)) + threadCtxStackSize;

		return createContextArch(isUser, rip, newRsp);
	}
}