#include "Scheduler.hpp"

#include "CommonMain.hpp"
#include "Futex.hpp"
#include "IDAllocator.hpp"
#include "Main.hpp"
#include "PortMessaging.hpp"
#include "hal/Syscall.hpp"
#include "programs/Elf.hpp"
#include "utils/Asm.hpp"

namespace kernel::common::threading {
	using namespace programs;

	bool Scheduler::isDisabled = false;

	namespace {
		constexpr u64 unlockedCoreId = ~0x0U;
		u64 nextThreadGeneration = 1;

		auto allocateThreadGeneration() -> u64 {
			const u64 generation = __atomic_fetch_add(&nextThreadGeneration, 1, __ATOMIC_RELAXED);

			return generation == 0 ? __atomic_fetch_add(&nextThreadGeneration, 1, __ATOMIC_RELAXED) : generation;
		}

		auto selectTargetExecutionNode(Thread *thread) -> ExecutionNode * {
			if (thread == nullptr) {
				return Scheduler::getCurrentExecutionNode();
			}

			if (thread->getLockedCoreId() != unlockedCoreId) {
				ExecutionNode *lockedNode = Scheduler::getCoreEN(thread->getLockedCoreId());

				if (lockedNode != nullptr) {
					return lockedNode;
				}

				thread->setLockedCoreId(unlockedCoreId);
			}

			ExecutionNode *bestNode = Scheduler::getCurrentExecutionNode();

			if (bestNode == nullptr) {
				return nullptr;
			}

			const auto queueSize = [](ExecutionNode *node) -> usize {
				const bool prevIF = node->coreLock.lock();
				const usize size = node->uleQueue.size();
				node->coreLock.unlock(prevIF);

				return size;
			};

			usize bestSize = queueSize(bestNode);
			auto *kernel = reinterpret_cast<x86_64::Kernel *>(CommonMain::getInstance());
			const x86_64::CpuManager *cpuManager = kernel->getCpuManager();

			const auto considerNode = [&bestNode, &bestSize, &queueSize](ExecutionNode *node) -> void {
				if (node == nullptr) {
					return;
				}

				const usize size = queueSize(node);

				if (size < bestSize) {
					bestNode = node;
					bestSize = size;
				}
			};

			if (cpuManager->getBootstrapCpu() != nullptr) {
				considerNode(&cpuManager->getBootstrapCpu()->executionNode);
			}

			if (cpuManager->getCoreList() != nullptr && cpuManager->getCoreAmount() > 1) {
				for (u64 i = 0; i < cpuManager->getCoreAmount() - 1; i++) {
					considerNode(&cpuManager->getCoreList()[i].cpuCore.executionNode);
				}
			}

			return bestNode;
		}
	}

	// Threads

	Thread::Thread(Scheduler *scheduler, Process* parent, const u64 rip, const bool isUser, const u64 rsp, const u64 userRsp, const bool is32Bit, const ThreadOS os) : parent(parent), bit32(is32Bit), os(os), lockedCoreId(~0x0U) {
		this->context = scheduler->createContext(this, parent, isUser, rip, rsp, userRsp);

		this->id = TIDAllocator::allocTID();
		this->generation = allocateThreadGeneration();
	}

	Thread::Thread(Process* parent, u64 *context) : parent(parent), context(context), lockedCoreId(~0x0U) {
		this->id = TIDAllocator::allocTID();
		this->generation = allocateThreadGeneration();
	}

	Thread::~Thread() {
		AllocContext *ctx = CommonMain::getInstance()->getKernelAllocContext();

		TIDAllocator::freeTID(this->id);

		if (this->syscallStackPointer != 0) {
			VirtualAllocator::free(ctx, reinterpret_cast<u64 *>(this->syscallStackPointer - threadCtxStackSize));
		}

		if (this->kernelStackOwned && this->kernelStackPointer != 0) {
			VirtualAllocator::free(ctx, reinterpret_cast<u64 *>(this->kernelStackPointer - threadCtxStackSize));
		}

		if (this->context != nullptr) {
			this->deleteThreadArch();

			if (this->parent != nullptr) {
				VirtualAllocator::free(ctx, this->context);
			}
		}
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

	void Thread::setWaitingPort(const u64 port) {
		this->waitingPort = port;
	}

	u64 Thread::getWaitingPort() const {
		return this->waitingPort;
	}

	void Thread::queueSignal(const u64 signal) {
		this->pendingSignal = signal;
		this->signalPending = true;
	}

	bool Thread::hasPendingSignal() const {
		return this->signalPending;
	}

	u64 Thread::getPendingSignal() const {
		return this->pendingSignal;
	}

	bool Thread::hasSignalFrame() const {
		return this->signalFrameValid;
	}

	void Thread::setSignalFrame(const SignalContext &frame) {
		this->signalFrame = frame;
		this->signalFrameValid = true;
	}

	const SignalContext &Thread::getSignalFrame() const {
		return this->signalFrame;
	}

	void Thread::clearPendingSignal() {
		this->signalPending = false;
		this->pendingSignal = 0;
	}

	void Thread::clearSignalFrame() {
		this->signalFrameValid = false;
		this->signalFrame = {};
	}

	void Thread::clearSignalState() {
		this->clearPendingSignal();
		this->clearSignalFrame();
	}

	void Thread::setStackPointer(const u64 newStackPointer) {
		this->stackPointer = newStackPointer;
	}

	u64 *Thread::getStackPointer() {
		return &this->stackPointer;
	}

	void Thread::setKStackPointer(const u64 newKStackPointer) {
		this->kernelStackPointer = newKStackPointer;
	}

	u64 Thread::getKStackPointer() const {
		return this->kernelStackPointer;
	}

	void Thread::setKernelStackOwned(const bool owned) {
		this->kernelStackOwned = owned;
	}

	void Thread::setSyscallStackPointer(const u64 newSyscallStackPointer) {
		this->syscallStackPointer = newSyscallStackPointer;
	}

	u64 Thread::getSyscallStackPointer() const {
		return this->syscallStackPointer;
	}

	bool Thread::is32Bit() const {
		return this->bit32;
	}

	ThreadOS Thread::getOS() const {
		return this->os;
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

	u64 Thread::getGeneration() const {
		return this->generation;
	}

	Process *Thread::getParent() const {
		return this->parent;
	}

	u64 Thread::getLockedCoreId() const {
		return this->lockedCoreId;
	}

	void Thread::setLockedCoreId(const u64 newId) {
		this->lockedCoreId = newId;
	}

	void Thread::setPendingWakeup(const bool val) {
		this->pendingWakeup = val;
	}

	bool Thread::getPendingWakeup() const {
		return this->pendingWakeup;
	}

	void Thread::setQueuedExecutionNode(ExecutionNode *node) {
		this->queuedExecutionNode = node;
	}

	ExecutionNode *Thread::getQueuedExecutionNode() const {
		return this->queuedExecutionNode;
	}

	void Thread::setQueued(const bool val) {
		this->queued = val;
	}

	bool Thread::isQueued() const {
		return this->queued;
	}

	u8 Thread::computeInteractiveScore() const {
		if (sleepTime + runTime == 0) {
			return 50;
		}

		return static_cast<u8>((sleepTime * 100) / (sleepTime + runTime));
	}

	// TODO
	void Thread::recomputeDynPriority() {
		const u64 total = this->runTime + this->sleepTime;

		if (total == 0) {
			this->dynPriority = 128; return;
		}

		// score: 0 = fully CPU-bound (high dynPriority = low urgency)
		//        255 = fully interactive (low dynPriority = high urgency)
		const u8 score = static_cast<u8>((this->sleepTime * 255) / total);
		this->dynPriority = 255 - score; // invert: interactive → small key → runs first
	}

	// Process

	Process::Process(const ProcessPriority priority, const bool isUserspace) : isUserspace(isUserspace), processContextOwned(true), priority(priority) {
		this->id = PIDAllocator::allocPID();

		auto [ctx, ctkKern] = VirtualAllocator::createProcessContext();

		this->processContext = ctx;
		this->processContextKernel = ctkKern;
	}

	Process::Process(const ProcessPriority priority, AllocContext *context) : processContext(context), processContextKernel(context), processContextOwned(false), priority(priority) {
		this->id = PIDAllocator::allocPID();
	}

	Process::~Process() {
		PIDAllocator::freePID(this->id);

		/*const LinkedListEntry<Thread> *tmpEntry = this->threadList.getFirst();

		while (tmpEntry != nullptr) {
			const LinkedListEntry<Thread> *newTmpEntry = tmpEntry;
			tmpEntry = tmpEntry->next;

			CommonMain::getInstance()->getScheduler()->killThread(newTmpEntry);
		}*/

		if (this->processContextOwned) {
			VirtualAllocator::destroyContext(this->processContext);
		}
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

	bool Process::ownsProcessContext() const {
		return this->processContextOwned;
	}

	bool Process::isTerminating() const {
		return this->terminating;
	}

	void Process::setTerminating(const bool val) {
		this->terminating = val;
	}

	LinkedListEntry<Thread> *Process::addThread(Thread *entry) {
		return this->threadList.addStart(entry, false);
	}

	void Process::removeThread(Thread *entry) {
		this->threadList.remove(entry, false, false);
	}

	u16 Process::getId() const {
		return this->id;
	}

	// Execution Node

	ExecutionNode::ExecutionNode() {
		this->uleQueue.init(this);
	}

	void ExecutionNode::init() {
		Scheduler *schedulerPtr = CommonMain::getInstance()->getScheduler();

		Process *idleProcess = schedulerPtr->getProcess(0);

		auto *newThread = new Thread(schedulerPtr, idleProcess, reinterpret_cast<u64>(&idleThreadFun), false, this->getENThreadRsp());

		newThread->setState(ThreadState::RUNNING);

		schedulerPtr->getProcess(0)->addThread(newThread);

		this->idleThread = newThread;
		this->currentThread = newThread;
	}

	void ExecutionNode::setCurrentThread(Thread *thread) {
		this->currentThread = thread;
	}

	Thread *ExecutionNode::getCurrentThread() const {
		return this->currentThread;
	}

	Thread *ExecutionNode::getIdleThread() const {
		return this->idleThread;
	}

	Thread* ExecutionNode::getNextThread() {
		Thread *next = this->uleQueue.dequeueMin();

		if (next == nullptr) {
			next = this->idleThread;
		}

		return next;
	}

	void ExecutionNode::enqueueThread(Thread *thread, const bool waking) {
		if (thread == nullptr or (this->idleThread != nullptr and thread == this->idleThread)) {
			return;
		}

		const bool prevCoreIF = this->coreLock.lock();

		if (thread->isQueued()) {
			this->coreLock.unlock(prevCoreIF);

			return;
		}

		thread->recomputeDynPriority();

		if (waking) {
			this->uleQueue.enqueueWaking(thread, thread->dynPriority);
		} else {
			this->uleQueue.enqueue(thread, thread->dynPriority);
		}

		this->coreLock.unlock(prevCoreIF);
	}

	bool ExecutionNode::removeThread(Thread *thread) {
		if (thread == nullptr) {
			return false;
		}

		const bool prevCoreIF = this->coreLock.lock();
		const bool removed = this->uleQueue.remove(thread);
		this->coreLock.unlock(prevCoreIF);

		return removed;
	}

	bool ExecutionNode::hasRunnableThreads() const {
		return !this->uleQueue.isEmpty();
	}

	bool ExecutionNode::isDisabled() const {
		return this->isDisabledFlag;
	}

	void ExecutionNode::setDisabled(const bool val) {
		this->isDisabledFlag = val;
	}

	void ExecutionNode::setPendingSchedUnlock(const bool prevIF) {
		this->pendingSchedUnlock = true;
		this->pendingSchedUnlockIF = prevIF;
	}

	bool ExecutionNode::hasPendingSchedUnlock() const {
		return this->pendingSchedUnlock;
	}

	bool ExecutionNode::consumePendingSchedUnlock() {
		this->pendingSchedUnlock = false;

		return this->pendingSchedUnlockIF;
	}

	// Reaper Thread

	[[noreturn]] void reaperFunction() {
		Scheduler *scheduler = CommonMain::getInstance()->getScheduler();

		for (;;) {
			auto *currThread = Scheduler::getCurrentThread();
			const bool prevIF = scheduler->getSchedLock()->lock();

			while (const auto *entry = scheduler->awaitingKillThreadList.removeFirstEntry(false)) {
				Scheduler::getCurrentExecutionNode()->setDisabled(true);

				scheduler->reaperThreadArch(entry);

				Scheduler::getCurrentExecutionNode()->setDisabled(false);
			}

			const LinkedListEntry<Process> *currProcessEntry = scheduler->processList.getFirst();

			while (currProcessEntry != nullptr) {
				const LinkedListEntry<Process> *tmpEntry = currProcessEntry->next;

				if (currProcessEntry->value->ownsProcessContext() && currProcessEntry->value->threadList.getSize() == 0) {
					Scheduler::getCurrentExecutionNode()->setDisabled(true);

					scheduler->reaperProcessArch(currProcessEntry->value);

					Scheduler::getCurrentExecutionNode()->setDisabled(false);
				}

				currProcessEntry = tmpEntry;
			}

			scheduler->getSchedLock()->unlock(prevIF);

			scheduler->sleepThread(currThread, 500ull * 1'000'000ull); // TODO: ms to ns and vice versa function
		}
	}

	// Scheduler

	Scheduler::Scheduler() {
		this->addProcess(new Process(ProcessPriority::LOW, CommonMain::getInstance()->getKernelAllocContext()));

		auto *reaperProcess = new Process(ProcessPriority::VERY_HIGH, CommonMain::getInstance()->getKernelAllocContext());

		this->addProcess(reaperProcess);

		this->addThread(false, reinterpret_cast<u64>(&reaperFunction), reaperProcess);

		auto *terminalProcess = new Process(ProcessPriority::VERY_HIGH, CommonMain::getInstance()->getKernelAllocContext());

		this->addProcess(terminalProcess);

		this->addThread(false, reinterpret_cast<u64>(&terminalThreadFunction), terminalProcess);
	}

	Process *Scheduler::getProcess(const u16 pid) {
		const bool prevIF = this->schedLock.lock();

		for (auto &currEntry : this->processList) {
			if (currEntry.getId() == pid) {
				this->schedLock.unlock(prevIF);

				return &currEntry;
			}
		}

		this->schedLock.unlock(prevIF);

		return nullptr;
	}

	Thread *Scheduler::getThread(const Process *process, const u16 tid) {
		if (process == nullptr) {
			return nullptr;
		}

		for (auto &currEntry : process->threadList) {
			if (currEntry.getId() == tid) {
				return &currEntry;
			}
		}

		return nullptr;
	}

	Thread *Scheduler::getThread(const u16 tid) {
		for (auto &process : this->processList) {
			for (auto &currEntry : process.threadList) {
				if (currEntry.getId() == tid) {
					return &currEntry;
				}
			}
		}

		return nullptr;
	}

	LinkedListEntry<Process> *Scheduler::addProcess(Process *process) {
		const bool prevIF = this->schedLock.lock();

		auto *entry = this->processList.addStart(process, false);

		this->schedLock.unlock(prevIF);

		return entry;
	}

	void Scheduler::killProcess(Process *process) {
		if (process == nullptr) {
			return;
		}

		const u16 killedPid = process->getId();
		const bool prevIF = this->schedLock.lock();

		if (process->isTerminating()) {
			Thread *current = getCurrentExecutionNode()->getCurrentThread();
			const bool killCurrent = current != nullptr && current->getParent() == process && current->getState() != ThreadState::TERMINATED;
			this->schedLock.unlock(prevIF);

			if (killCurrent) {
				this->killThread(current);
			}

			return;
		}

		process->setTerminating(true);
		this->schedLock.unlock(prevIF);
		hal::SyscallManager::notifyProcessKilled(killedPid);

		const LinkedListEntry<Thread> *tmpEntry = process->threadList.getFirst();

		while (tmpEntry != nullptr) {
			const LinkedListEntry<Thread> *nextEntry = tmpEntry->next;

			this->killThread(tmpEntry->value);

			tmpEntry = nextEntry;
		}
	}

	LinkedListEntry<Thread> *Scheduler::addThread(const bool isUser, const u64 rip, Process *process) {
		auto *newThread = new Thread(this, process, rip, isUser);

		if (newThread == nullptr || newThread->getContext() == nullptr) {
			delete newThread;

			return nullptr;
		}

		newThread->setState(ThreadState::READY);

		const bool prevIF = this->schedLock.lock();

		LinkedListEntry<Thread> *entry = process->addThread(newThread);
		this->enqueueThread(newThread);

		this->schedLock.unlock(prevIF);

		return entry;
	}

	void Scheduler::startProcess(const u64 startAddr, const ProcessPriority priority, const bool isUserspace) {
		auto *newProc = new Process(priority, isUserspace); // TODO: Maybe create process in its own context

		this->addProcess(newProc);

		this->addThread(isUserspace, startAddr, newProc);
	}

	bool Scheduler::startElfProcess(u64 *elfFile, const ProcessPriority priority, const bool isUserspace) {
		if (!Elf::isElf(reinterpret_cast<ElfCommonHeader *>(elfFile))) {
			return false;
		}

		auto *newProc = new Process(priority, isUserspace);

		u64 *loadedAddr = Elf::loadElf(elfFile, newProc, newProc->getProcessContext(), pageSize);

		if (loadedAddr == nullptr) {
			delete newProc;

			return false;
		}

		this->addProcess(newProc);

		this->addThread(isUserspace, reinterpret_cast<u64>(loadedAddr), newProc);

		return true;
	}

	// TODO: Fix
	void Scheduler::killThread(Thread *thread) {
		if (thread == nullptr) {
			return;
		}

		const u16 killedTid = thread->getId();
		const u16 killedPid = thread->getParent() != nullptr ? thread->getParent()->getId() : 0;
		const bool prevIF = this->schedLock.lock();
		bool notifyKilled = false;

		const bool shouldReschedule = getCurrentExecutionNode()->getCurrentThread() == thread;

		if (thread->getState() == ThreadState::TERMINATED) {
			this->schedLock.unlock(prevIF);

			if (shouldReschedule) {
				ExecutionNode::reSchedule();
			}

			return;
		}

		thread->setState(ThreadState::TERMINATED);
		thread->setWaitingPort(0);
		thread->setSleepNs(0);
		notifyKilled = true;

		if (!shouldReschedule) {
			if (this->removeThread(thread)) {
				this->awaitingKillThreadList.addEnd(thread, false);
			}
		}

		this->schedLock.unlock(prevIF);

		PortMessaging::removeThread(thread);
		Futex::removeThread(thread->getId());

		if (notifyKilled) {
			hal::SyscallManager::notifyThreadKilled(killedPid, killedTid);
		}

		if (shouldReschedule) {
			ExecutionNode::reSchedule();
		}
	}

	void Scheduler::killThread(const LinkedListEntry<Thread> *thread) {
		this->killThread(thread->value);
	}

	bool Scheduler::removeThread(Thread *thread) {
		if (thread == nullptr) {
			return false;
		}

		bool removed = false;

		if (thread->isQueued()) {
			ExecutionNode *node = thread->getQueuedExecutionNode();

			if (node != nullptr && node->removeThread(thread)) {
				removed = true;
			}
		}

		if (this->sleepingThreadList.remove(thread, false, false)) {
			removed = true;
		}

		if (this->blockedThreadList.remove(thread, false, false)) {
			removed = true;
		}

		return removed;
	}

	void Scheduler::enqueueThread(Thread *thread, const bool waking) {
		if (thread == nullptr) {
			return;
		}

		if (thread->isQueued()) {
			this->removeThread(thread);
		}

		ExecutionNode *target = selectTargetExecutionNode(thread);

		if (target == nullptr) {
			return;
		}

		thread->setState(ThreadState::READY);
		target->enqueueThread(thread, waking);
	}

	void Scheduler::enqueueThread(Thread *thread, ExecutionNode *target, const bool waking) {
		if (thread == nullptr || target == nullptr) {
			return;
		}

		if (thread->isQueued()) {
			this->removeThread(thread);
		}

		thread->setState(ThreadState::READY);
		target->enqueueThread(thread, waking);
	}

	bool Scheduler::hasRunnableThreads() {
		auto *kernel = reinterpret_cast<x86_64::Kernel *>(CommonMain::getInstance());
		const x86_64::CpuManager *cpuManager = kernel->getCpuManager();

		if (cpuManager->getBootstrapCpu() != nullptr && cpuManager->getBootstrapCpu()->executionNode.hasRunnableThreads()) {
			return true;
		}

		if (cpuManager->getCoreList() != nullptr && cpuManager->getCoreAmount() > 1) {
			for (u64 i = 0; i < cpuManager->getCoreAmount() - 1; i++) {
				if (cpuManager->getCoreList()[i].cpuCore.executionNode.hasRunnableThreads()) {
					return true;
				}
			}
		}

		return false;
	}

	void Scheduler::sleepThread(const u16 threadId, const u64 ns) {
		const bool prevIF = this->schedLock.lock();
		Thread *thread = this->getThread(threadId);

		if (thread == nullptr) {
			this->schedLock.unlock(prevIF);
			return;
		}

		const u64 now = CommonMain::getInstance()->getClocks()->getMainClock()->getNs();
		thread->setSleepNs(now + ns);

		//CommonMain::getTerminal()->debug("Sleep Ns: %llu for thread: %u", "Scheduler", thread->getSleepNs(), thread->getId());

		this->removeThread(thread);
		thread->setState(ThreadState::BLOCKED);
		thread->lastScheduledNs = now;

		const bool shouldReschedule = getCurrentExecutionNode()->getCurrentThread() == thread;

		if (!this->sleepingThreadList.contains(thread)) {
			this->sleepingThreadList.addStart(thread, false);
		}

		this->schedLock.unlock(prevIF);

		if (shouldReschedule) {
			ExecutionNode::reSchedule();
		}
	}

	void Scheduler::sleepThread(Thread *thread, const u64 ns) {
		const bool prevIF = this->schedLock.lock();

		const u64 now = CommonMain::getInstance()->getClocks()->getMainClock()->getNs();
		thread->setSleepNs(now + ns);

		//CommonMain::getTerminal()->debug("Sleep Ns: %llu for thread: %u", "Scheduler", thread->getSleepNs(), thread->getId());

		this->removeThread(thread);
		thread->setState(ThreadState::BLOCKED);
		thread->lastScheduledNs = now;

		const bool shouldReschedule = getCurrentExecutionNode()->getCurrentThread() == thread;

		if (!this->sleepingThreadList.contains(thread)) {
			this->sleepingThreadList.addStart(thread, false);
		}

		this->schedLock.unlock(prevIF);

		if (shouldReschedule) {
			ExecutionNode::reSchedule();
		}
	}

	void Scheduler::blockThread(const u16 threadId, const bool useLock) {
		bool prevIF = true;

		if (useLock) {
			prevIF = this->schedLock.lock();
		}

		Thread *thread = this->getThread(threadId);

		if (thread == nullptr) {
			if (useLock) {
				this->schedLock.unlock(prevIF);
			}

			return;
		}

		if (thread->getPendingWakeup()) {
			thread->setPendingWakeup(false);

			if (useLock) {
				this->schedLock.unlock(prevIF);
			}

			return;
		}

		/*if (thread->getWaitingPort() == 0) {
			this->schedLock.unlock(prevIF);

			return;
		}*/

		//CommonMain::getTerminal()->debug("Blocking thread: thread: %u", "Scheduler", thread->getId());

		this->removeThread(thread);
		thread->setState(ThreadState::BLOCKED);
		thread->lastScheduledNs = CommonMain::getInstance()->getClocks()->getMainClock()->getNs();

		const bool shouldReschedule = getCurrentExecutionNode()->getCurrentThread() == thread;

		//if (!shouldReschedule) {
			this->blockedThreadList.addStart(thread, false);
		//}

		if (useLock) {
			this->schedLock.unlock(prevIF);
		}

		if (shouldReschedule) {
			// TODO
			x86_64::utils::Asm::sti();
			ExecutionNode::reSchedule();
		}
	}

	void Scheduler::unblockThread(const u16 threadId, const bool top, const bool useLock) {
		bool prevIF = true;

		if (useLock) {
			prevIF = this->schedLock.lock();
		}

		Thread *thread = this->getThread(threadId);

		if (thread == nullptr) {
			if (useLock) {
				this->schedLock.unlock(prevIF);
			}

			return;
		}

		//CommonMain::getTerminal()->debug("Unblocking thread: thread: %u", "Scheduler", thread->getId());
		//thread->setWaitingPort(0);

		//const bool prevIF = this->schedLock.lock();

		const bool wasBlocked = this->blockedThreadList.remove(thread, false, false);
		const bool wasSleeping = this->sleepingThreadList.remove(thread, false, false);

		if (wasBlocked || wasSleeping) {
			thread->setWaitingPort(0);
			thread->setSleepNs(0);
			thread->setState(ThreadState::RUNNING);

			(void) top;
			this->enqueueThread(thread, true);
		} else {
			// Thread hasn't called blockThread yet — set the pending wakeup flag
			// so that the upcoming blockThread() call returns immediately.
			thread->setPendingWakeup(true);
		}

		if (useLock) {
			this->schedLock.unlock(prevIF);
		}
	}

	void Scheduler::unblockThreadIfWaiting(const u16 threadId, const u64 generation, const u64 port, const bool top, const bool useLock) {
		bool prevIF = true;

		if (useLock) {
			prevIF = this->schedLock.lock();
		}

		Thread *thread = this->getThread(threadId);

		if (thread == nullptr || thread->getGeneration() != generation || thread->getWaitingPort() != port) {
			if (useLock) {
				this->schedLock.unlock(prevIF);
			}

			return;
		}

		const bool wasBlocked = this->blockedThreadList.remove(thread, false, false);

		if (wasBlocked) {
			thread->setWaitingPort(0);
			thread->setSleepNs(0);
			thread->setState(ThreadState::RUNNING);

			(void) top;
			this->enqueueThread(thread, true);
		}

		if (useLock) {
			this->schedLock.unlock(prevIF);
		}
	}

	void Scheduler::sleepTick() {
		Scheduler *schedulerPtr = CommonMain::getInstance()->getScheduler();

		const bool prevIF = schedulerPtr->getSchedLock()->lock();

		auto it = schedulerPtr->sleepingThreadList.begin();
		auto end = schedulerPtr->sleepingThreadList.end();

		while (it != end) {
			auto &currEntry = *it;
			auto nextIt = it;
			++nextIt;

			if (currEntry.getSleepNs() > 0) {
				if (currEntry.getSleepNs() <= CommonMain::getInstance()->getClocks()->getMainClock()->getNs()) {
					//CommonMain::getTerminal()->debug("Wake thread: %u", "Scheduler", currEntry.getId());

					schedulerPtr->sleepingThreadList.remove(&currEntry, false, false);
					schedulerPtr->blockedThreadList.remove(&currEntry, false, false);

					currEntry.setState(ThreadState::RUNNING);
					currEntry.setSleepNs(0);

					const u64 now = CommonMain::getInstance()->getClocks()->getMainClock()->getNs();
					currEntry.sleepTime += now - currEntry.lastScheduledNs;
					currEntry.recomputeDynPriority();

					schedulerPtr->enqueueThread(&currEntry, true);
				}
			}

			it = nextIt;
		}

		schedulerPtr->getSchedLock()->unlock(prevIF);
	}

	TicketSpinLock *Scheduler::getSchedLock() {
		return &this->schedLock;
	}

	void Scheduler::debugDump() {
	    auto *term = CommonMain::getTerminal();
		const Scheduler *schedulerPtr = CommonMain::getInstance()->getScheduler();
	    auto *kernel = reinterpret_cast<x86_64::Kernel *>(CommonMain::getInstance());
	    const x86_64::CpuManager *cpuManager = kernel->getCpuManager();
		const auto dumpUleQueue = [term](const UleRunQueue &queue) -> void {
			bstree_node_t *node = queue.tree.root != nullptr ? bstree_minimum(queue.tree.root) : nullptr;

			while (node != nullptr) {
				const Thread *thread = container_of(node, &Thread::bstNode);

				term->printfBoth(true, "      TID=%u PID=%u state=%u dynPrio=%u schedKey=%lu waitingPort=%lu pendingWakeup=%u lockedCore=%lu",
					thread->getId(),
					thread->getParent()->getId(),
					static_cast<u32>(thread->getState()),
					static_cast<u32>(thread->dynPriority),
					thread->schedKey,
					thread->getWaitingPort(),
					static_cast<u32>(thread->getPendingWakeup()),
					thread->getLockedCoreId());

				node = bstree_successor(node);
			}
		};

	    term->printfBoth(true, "=== SCHEDULER DEBUG DUMP ===");

	    // ── Per-core running threads ──────────────────────────────────────────────
	    term->printfBoth(true, "  === RUNNING THREADS PER CORE ===");

	    // Bootstrap core
	    const x86_64::CpuCore *bsp = cpuManager->getBootstrapCpu();

	    if (bsp != nullptr) {
	        const auto *bspThread = bsp->executionNode.getCurrentThread();

	        if (bspThread != nullptr) {
	            term->printfBoth(true, "  Core CPU=%u (BSP): TID=%u PID=%u state=%u pendingWakeup=%u waitingPort=%lu",
	                bsp->cpuId,
	                bspThread->getId(),
	                bspThread->getParent()->getId(),
	                static_cast<u32>(bspThread->getState()),
	                static_cast<u32>(bspThread->getPendingWakeup()),
	                bspThread->getWaitingPort());
	        } else {
	            term->printfBoth(true, "  Core CPU=%u (BSP): no current thread", bsp->cpuId);
	        }

	    	term->printfBoth(true, "    ULEQueue size=%lu (BSP CPU=%u):", bsp->executionNode.uleQueue.size(), bsp->cpuId);
			dumpUleQueue(bsp->executionNode.uleQueue);
	    }

	    // AP cores
	    if (cpuManager->getCoreList() != nullptr && cpuManager->getCoreAmount() > 1) {
	        for (u64 i = 0; i < cpuManager->getCoreAmount() - 1; i++) {
	            const x86_64::CpuCore *core = &cpuManager->getCoreList()[i].cpuCore;
	            const auto *coreThread = core->executionNode.getCurrentThread();

	            if (coreThread != nullptr) {
	                term->printfBoth(true, "  Core CPU=%u (AP %lu): TID=%u PID=%u state=%u pendingWakeup=%u waitingPort=%lu",
	                    core->cpuId,
	                    i,
	                    coreThread->getId(),
	                    coreThread->getParent()->getId(),
	                    static_cast<u32>(coreThread->getState()),
	                    static_cast<u32>(coreThread->getPendingWakeup()),
	                    coreThread->getWaitingPort());
	            } else {
	                term->printfBoth(true, "  Core CPU=%u (AP %lu): no current thread", core->cpuId, i);
	            }

	        	term->printfBoth(true, "    ULEQueue size=%lu (AP CPU=%u):", core->executionNode.uleQueue.size(), core->cpuId);
				dumpUleQueue(core->executionNode.uleQueue);
	        }
	    }

	    // ── Blocked list ──────────────────────────────────────────────────────────
	    term->printfBoth(true, "  BlockedList size=%lu:", schedulerPtr->blockedThreadList.getSize());

	    for (const auto &t : schedulerPtr->blockedThreadList) {
	        term->printfBoth(true, "    TID=%u PID=%u waitingPort=%lu pendingWakeup=%u sleepNs=%lu",
	            t.getId(),
	            t.getParent()->getId(),
	            t.getWaitingPort(),
	            static_cast<u32>(t.getPendingWakeup()),
	            t.getSleepNs());
	    }

	    // ── Sleeping list ─────────────────────────────────────────────────────────
	    term->printfBoth(true, "  SleepingList size=%lu:", schedulerPtr->sleepingThreadList.getSize());

	    for (const auto &t : schedulerPtr->sleepingThreadList) {
	        term->printfBoth(true, "    TID=%u PID=%u sleepNs=%lu", t.getId(), t.getParent()->getId(), t.getSleepNs());
	    }

	    // ── Port messaging ────────────────────────────────────────────────────────
	    PortMessaging::debugDump();

	    term->printfBoth(true, "=== END DUMP ===");
	}
}
