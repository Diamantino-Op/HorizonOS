#include "Scheduler.hpp"

#include "CommonMain.hpp"
#include "Futex.hpp"
#include "IDAllocator.hpp"
#include "Main.hpp"
#include "PortMessaging.hpp"
#include "programs/Elf.hpp"
#include "utils/Asm.hpp"

namespace kernel::common::threading {
	using namespace programs;

	bool Scheduler::isDisabled = false;

	// Threads

	Thread::Thread(Scheduler *scheduler, Process* parent, const u64 rip, const bool isUser, const u64 rsp, const u64 userRsp, const bool is32Bit, const ThreadOS os) : parent(parent), bit32(is32Bit), os(os), lockedCoreId(~0x0U) {
		this->context = scheduler->createContext(this, parent, isUser, rip, rsp, userRsp);

		this->id = TIDAllocator::allocTID();
	}

	Thread::Thread(Process* parent, u64 *context) : parent(parent), context(context), lockedCoreId(~0x0U) {
		this->id = TIDAllocator::allocTID();
	}

	Thread::~Thread() {
		TIDAllocator::freeTID(this->id);

		if (this->syscallStackPointer != 0) {
			VirtualAllocator::free(this->parent->getProcessContext(), reinterpret_cast<u64 *>(this->syscallStackPointer));
		}

		if (this->context != nullptr) {
			this->deleteThreadArch();

			if (this->parent != nullptr) {
				VirtualAllocator::free(this->parent->getProcessContext(), this->context);
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

	// Process

	Process::Process(const ProcessPriority priority, const bool isUserspace) : isUserspace(isUserspace), priority(priority) {
		this->id = PIDAllocator::allocPID();

		auto [ctx, ctkKern] = VirtualAllocator::createProcessContext();

		this->processContext = ctx;
		this->processContextKernel = ctkKern;
	}

	Process::Process(const ProcessPriority priority, AllocContext *context) : processContext(context), processContextKernel(context), priority(priority) {
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

	LinkedListEntry<Thread> *Process::	addThread(Thread *entry) {
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

		auto *newThread = new Thread(schedulerPtr, idleProcess, reinterpret_cast<u64>(&idleThreadFun), false, this->getENThreadRsp());

		newThread->setState(ThreadState::RUNNING);

		schedulerPtr->getProcess(0)->addThread(newThread);

		this->idleThread = new LinkedListEntry<Thread>();
		this->idleThread->value = newThread;

		this->currentThread = this->idleThread;
	}

	void ExecutionNode::setCurrentThread(LinkedListEntry<Thread> *thread) {
		this->currentThread = thread;
	}

	LinkedListEntry<Thread> *ExecutionNode::getCurrentThread() const {
		return this->currentThread;
	}

	LinkedListEntry<Thread> *ExecutionNode::getIdleThread() const {
		return this->idleThread;
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

			while (const auto *entry = scheduler->awaitingKillThreadList.removeFirstEntry()) {
				Scheduler::getCurrentExecutionNode()->setDisabled(true);

				scheduler->reaperThreadArch(entry);

				Scheduler::getCurrentExecutionNode()->setDisabled(false);
			}

			const LinkedListEntry<Process> *currProcessEntry = scheduler->processList.getFirst();

			while (currProcessEntry != nullptr) {
				const LinkedListEntry<Process> *tmpEntry = currProcessEntry->next;

				if (currProcessEntry->value->threadList.getSize() == 0) {
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
		const bool prevIF = this->schedLock.lock();

		for (auto &currEntry : this->queues[process->getPriority()]) {
			if (currEntry.getId() == tid) {
				this->schedLock.unlock(prevIF);

				return &currEntry;
			}
		}

		this->schedLock.unlock(prevIF);

		return nullptr;
	}

	Thread *Scheduler::getThread(const u16 tid) {
		Thread *currThread = this->getCurrentExecutionNode()->getCurrentThread()->value;

		if (currThread->getId() == tid) {
			return currThread;
		}

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
		const bool prevIF = this->schedLock.lock();

		auto *entry = this->processList.addStart(process);

		this->schedLock.unlock(prevIF);

		return entry;
	}

	void Scheduler::killProcess(Process *process) {
		if (process == nullptr) {
			return;
		}

		const LinkedListEntry<Thread> *tmpEntry = process->threadList.getFirst();

		while (tmpEntry != nullptr) {
			const LinkedListEntry<Thread> *nextEntry = tmpEntry->next;

			this->killThread(tmpEntry->value);

			tmpEntry = nextEntry;
		}
	}

	LinkedListEntry<Thread> *Scheduler::addThread(const bool isUser, const u64 rip, Process *process) {
		auto *newThread = new Thread(this, process, rip, isUser);

		newThread->setState(ThreadState::READY);

		const bool prevIF = this->schedLock.lock();

		process->addThread(newThread);

		LinkedListEntry<Thread> *entry = nullptr;

		if (isUser) {
			entry = this->readyThreadList.addStart(newThread);
		} else {
			entry = this->readyThreadList.addEnd(newThread);
		}

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
		const bool prevIF = this->schedLock.lock();

		thread->setState(ThreadState::TERMINATED);
		PortMessaging::removeThread(thread);
		Futex::removeThread(thread->getId());

		if (thread->getParent() != nullptr) {
			thread->getParent()->removeThread(thread);
		}

		const bool shouldReschedule = getCurrentExecutionNode()->getCurrentThread()->value == thread;

		if (!shouldReschedule) {
			if (this->removeThread(thread)) {
				this->awaitingKillThreadList.addEnd(thread);
			}
		}

		this->schedLock.unlock(prevIF);

		if (shouldReschedule) {
			ExecutionNode::reSchedule();
		}
	}

	void Scheduler::killThread(const LinkedListEntry<Thread> *thread) {
		this->killThread(thread->value);
	}

	bool Scheduler::removeThread(Thread *thread) {
		if (this->queues[thread->getParent()->getPriority()].remove(thread, false)) {
			return true;
		}

		if (this->sleepingThreadList.remove(thread, false)) {
			return true;
		}

		if (this->blockedThreadList.remove(thread, false)) {
			return true;
		}

		return this->readyThreadList.remove(thread, false);
	}

	void Scheduler::sleepThread(const u16 threadId, const u64 ns) {
		const bool prevIF = this->schedLock.lock();
		Thread *thread = this->getThread(threadId);

		if (thread == nullptr) {
			this->schedLock.unlock(prevIF);
			return;
		}

		thread->setSleepNs(CommonMain::getInstance()->getClocks()->getMainClock()->getNs() + ns);

		//CommonMain::getTerminal()->debug("Sleep Ns: %llu for thread: %u", "Scheduler", thread->getSleepNs(), thread->getId());

		thread->setState(ThreadState::BLOCKED);

		this->queues[thread->getParent()->getPriority()].remove(thread, false);

		const bool shouldReschedule = getCurrentExecutionNode()->getCurrentThread()->value == thread;

		if (!this->sleepingThreadList.contains(thread)) {
			this->sleepingThreadList.addStart(thread);
		}

		this->schedLock.unlock(prevIF);

		if (shouldReschedule) {
			ExecutionNode::reSchedule();
		}
	}

	void Scheduler::sleepThread(Thread *thread, const u64 ns) {
		const bool prevIF = this->schedLock.lock();

		thread->setSleepNs(CommonMain::getInstance()->getClocks()->getMainClock()->getNs() + ns);

		//CommonMain::getTerminal()->debug("Sleep Ns: %llu for thread: %u", "Scheduler", thread->getSleepNs(), thread->getId());

		thread->setState(ThreadState::BLOCKED);

		this->queues[thread->getParent()->getPriority()].remove(thread, false);

		const bool shouldReschedule = getCurrentExecutionNode()->getCurrentThread()->value == thread;

		if (!this->sleepingThreadList.contains(thread)) {
			this->sleepingThreadList.addStart(thread);
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

		thread->setState(ThreadState::BLOCKED);

		if (!this->queues[thread->getParent()->getPriority()].remove(thread, false) and thread->getSleepNs() > 0) {
			this->sleepingThreadList.remove(thread, false);
		}

		const bool shouldReschedule = getCurrentExecutionNode()->getCurrentThread()->value == thread;

		if (!shouldReschedule) {
			this->blockedThreadList.addStart(thread);
		}

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

		if (wasBlocked) {
			thread->setWaitingPort(0);

			if (thread->getSleepNs() == 0) {
				thread->setState(ThreadState::RUNNING);

				if (top) {
					this->queues[thread->getParent()->getPriority()].addStart(thread, false);
				} else {
					this->queues[thread->getParent()->getPriority()].addEnd(thread, false);
				}
			} else {
				if (!this->sleepingThreadList.contains(thread)) {
					this->sleepingThreadList.addStart(thread, false);
				}
			}
		} else {
			// Thread hasn't called blockThread yet — set the pending wakeup flag
			// so that the upcoming blockThread() call returns immediately.
			thread->setPendingWakeup(true);
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

					currEntry.setState(ThreadState::RUNNING);

					currEntry.setSleepNs(0);

					schedulerPtr->sleepingThreadList.remove(&currEntry, false);

					if (currEntry.getLockedCoreId() == ~0x0U) {
						schedulerPtr->queues[currEntry.getParent()->getPriority()].addEnd(&currEntry);
					} else {
						ExecutionNode *node = getCoreEN(currEntry.getLockedCoreId());

						if (node == nullptr) {
							schedulerPtr->queues[currEntry.getParent()->getPriority()].addEnd(&currEntry);

							currEntry.setLockedCoreId(~0x0U);
						} else {
							node->lockedThreadQueues[currEntry.getParent()->getPriority()].addEnd(&currEntry);
						}
					}
				}
			}

			it = nextIt;
		}

		schedulerPtr->getSchedLock()->unlock(prevIF);
	}

	bool Scheduler::hasThreads() const {
		const bool prevIF = const_cast<TicketSpinLock &>(this->schedLock).lock();

		if (this->readyThreadList.getSize() > 0) {
			const_cast<TicketSpinLock &>(this->schedLock).unlock(prevIF);

			return true;
		}

		for (auto &currEntry : this->queues) {
			if (currEntry.getSize() > 0) {
				const_cast<TicketSpinLock &>(this->schedLock).unlock(prevIF);

				return true;
			}
		}

		const_cast<TicketSpinLock &>(this->schedLock).unlock(prevIF);

		return false;
	}

	TicketSpinLock *Scheduler::getSchedLock() {
		return &this->schedLock;
	}

	void Scheduler::debugDump() {
	    auto *term = CommonMain::getTerminal();
	    Scheduler *schedulerPtr = CommonMain::getInstance()->getScheduler();
	    auto *kernel = reinterpret_cast<x86_64::Kernel *>(CommonMain::getInstance());
	    const x86_64::CpuManager *cpuManager = kernel->getCpuManager();

	    term->warnNoLock("=== SCHEDULER DEBUG DUMP ===", "SchedDump");

	    // ── Per-core running threads ──────────────────────────────────────────────
	    term->warnNoLock("  === RUNNING THREADS PER CORE ===", "SchedDump");

	    // Bootstrap core
	    const x86_64::CpuCore *bsp = cpuManager->getBootstrapCpu();

	    if (bsp != nullptr) {
	        const auto *bspThread = bsp->executionNode.getCurrentThread();

	        if (bspThread != nullptr && bspThread->value != nullptr) {
	            term->warnNoLock("  Core CPU=%u (BSP): TID=%u PID=%u state=%u pendingWakeup=%u waitingPort=%lu",
	                "SchedDump",
	                bsp->cpuId,
	                bspThread->value->getId(),
	                bspThread->value->getParent()->getId(),
	                static_cast<u32>(bspThread->value->getState()),
	                static_cast<u32>(bspThread->value->getPendingWakeup()),
	                bspThread->value->getWaitingPort());
	        } else {
	            term->warnNoLock("  Core CPU=%u (BSP): no current thread", "SchedDump", bsp->cpuId);
	        }

	    	// BSP locked thread queues
	    	for (usize priority = 0; priority < ProcessPriority::COUNT; ++priority) {
	    		const auto &lq = bsp->executionNode.lockedThreadQueues[priority];

	    		if (lq.getSize() == 0) {
	    			continue;
	    		}

	    		term->warnNoLock("    LockedQueue[%lu] size=%lu (BSP CPU=%u):",
					"SchedDump", priority, lq.getSize(), bsp->cpuId);

	    		for (const auto &t : lq) {
	    			term->warnNoLock("      TID=%u PID=%u state=%u waitingPort=%lu pendingWakeup=%u",
						"SchedDump",
						t.getId(),
						t.getParent()->getId(),
						static_cast<u32>(t.getState()),
						t.getWaitingPort(),
						static_cast<u32>(t.getPendingWakeup()));
	    		}
	    	}
	    }

	    // AP cores
	    if (cpuManager->getCoreList() != nullptr && cpuManager->getCoreAmount() > 1) {
	        for (u64 i = 0; i < cpuManager->getCoreAmount() - 1; i++) {
	            const x86_64::CpuCore *core = &cpuManager->getCoreList()[i].cpuCore;
	            const auto *coreThread = core->executionNode.getCurrentThread();

	            if (coreThread != nullptr && coreThread->value != nullptr) {
	                term->warnNoLock("  Core CPU=%u (AP %lu): TID=%u PID=%u state=%u pendingWakeup=%u waitingPort=%lu",
	                    "SchedDump",
	                    core->cpuId,
	                    i,
	                    coreThread->value->getId(),
	                    coreThread->value->getParent()->getId(),
	                    static_cast<u32>(coreThread->value->getState()),
	                    static_cast<u32>(coreThread->value->getPendingWakeup()),
	                    coreThread->value->getWaitingPort());
	            } else {
	                term->warnNoLock("  Core CPU=%u (AP %lu): no current thread", "SchedDump", core->cpuId, i);
	            }

	        	// AP locked thread queues
	        	for (usize priority = 0; priority < ProcessPriority::COUNT; ++priority) {
	        		const auto &lq = core->executionNode.lockedThreadQueues[priority];

	        		if (lq.getSize() == 0) {
	        			continue;
	        		}

	        		term->warnNoLock("    LockedQueue[%lu] size=%lu (AP CPU=%u):",
						"SchedDump", priority, lq.getSize(), core->cpuId);

	        		for (const auto &t : lq) {
	        			term->warnNoLock("      TID=%u PID=%u state=%u waitingPort=%lu pendingWakeup=%u",
							"SchedDump",
							t.getId(),
							t.getParent()->getId(),
							static_cast<u32>(t.getState()),
							t.getWaitingPort(),
							static_cast<u32>(t.getPendingWakeup()));
	        		}
	        	}
	        }
	    }

	    // ── Run queues ────────────────────────────────────────────────────────────
	    for (usize priority = 0; priority < ProcessPriority::COUNT; ++priority) {
	        auto &q = schedulerPtr->queues[priority];

	        if (q.getSize() == 0) {
	            continue;
	        }

	        term->warnNoLock("  RunQueue[%lu] size=%lu:", "SchedDump", priority, q.getSize());

	        for (const auto &t : q) {
	            term->warnNoLock("    TID=%u PID=%u state=%u waitingPort=%lu pendingWakeup=%u",
	                "SchedDump",
	                t.getId(),
	                t.getParent()->getId(),
	                static_cast<u32>(t.getState()),
	                t.getWaitingPort(),
	                static_cast<u32>(t.getPendingWakeup()));
	        }
	    }

	    // ── Blocked list ──────────────────────────────────────────────────────────
	    term->warnNoLock("  BlockedList size=%lu:", "SchedDump", schedulerPtr->blockedThreadList.getSize());

	    for (const auto &t : schedulerPtr->blockedThreadList) {
	        term->warnNoLock("    TID=%u PID=%u waitingPort=%lu pendingWakeup=%u sleepNs=%lu",
	            "SchedDump",
	            t.getId(),
	            t.getParent()->getId(),
	            t.getWaitingPort(),
	            static_cast<u32>(t.getPendingWakeup()),
	            t.getSleepNs());
	    }

	    // ── Sleeping list ─────────────────────────────────────────────────────────
	    term->warnNoLock("  SleepingList size=%lu:", "SchedDump", schedulerPtr->sleepingThreadList.getSize());

	    for (const auto &t : schedulerPtr->sleepingThreadList) {
	        term->warnNoLock("    TID=%u PID=%u sleepNs=%lu", "SchedDump",
	            t.getId(), t.getParent()->getId(), t.getSleepNs());
	    }

	    // ── Ready list ────────────────────────────────────────────────────────────
	    term->warnNoLock("  ReadyList size=%lu:", "SchedDump", schedulerPtr->readyThreadList.getSize());

	    for (const auto &t : schedulerPtr->readyThreadList) {
	        term->warnNoLock("    TID=%u PID=%u", "SchedDump", t.getId(), t.getParent()->getId());
	    }

	    // ── Port messaging ────────────────────────────────────────────────────────
	    PortMessaging::debugDump();

	    term->warnNoLock("=== END DUMP ===", "SchedDump");
	}
}

