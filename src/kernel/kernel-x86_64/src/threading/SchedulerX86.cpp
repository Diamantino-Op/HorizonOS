#include "SchedulerX86.hpp"

#include "CommonMain.hpp"
#include "Main.hpp"
#include "Math.hpp"
#include "Time.hpp"
#include "hal/Interrupts.hpp"

#include "hal/Cpu.hpp"
#include "hal/Hpet.hpp"
#include "utils/Asm.hpp"
#include "utils/CpuId.hpp"

namespace kernel::common::threading {
	using namespace x86_64;
	using namespace x86_64::threading;
	using namespace x86_64::utils;

	void idleThreadFun() {
		for (;;) {
			asm volatile ("pause" ::: "memory");
		}
	}

	void Thread::deleteThreadArch() const {
		auto *threadContext = reinterpret_cast<ThreadContext *>(this->context);

		threadContext->~ThreadContext();
	}

	// TODO: Move to non arch
	void Scheduler::initArch() {
		CpuManager::getCurrentCore()->coreClock.addTimerHandle(&sleepTick, TimeUtils::msToNs(10));
	}

	Thread *Scheduler::getCurrentThread() {
		if (CpuManager::getCurrentCore() == nullptr or CpuManager::getCurrentCore()->executionNode.getCurrentThread() == nullptr) {
			return nullptr;
		}

		return CpuManager::getCurrentCore()->executionNode.getCurrentThread();
	}

	void Scheduler::timerReSchedule() {
		Interrupts::sendEOI();

		switchContextAsm();
	}

	auto Scheduler::intReSchedule(u64 *) -> u32 {
		Interrupts::sendEOI();

		switchContextAsm();

		return 10000;
	}

	void ExecutionNode::reSchedule() {
		CpuManager::getCurrentCore()->coreClock.resetSchedulerTimer();

		switchContextAsm();
	}

	extern "C" u64 checkDisabled() {
		const CpuCore *currentCore = CpuManager::getCurrentCore();
		const ExecutionNode &currentNode = currentCore->executionNode;
		const Thread *currentThread = Scheduler::getCurrentThread();

		if (currentNode.isDisabled() or Scheduler::isDisabled) {
			return 1;
		}

		if (currentThread == currentNode.getIdleThread()) {
			return 0;
		}

		if (currentThread->getState() != ThreadState::RUNNING) {
			return 0;
		}

		if (currentThread->getLockedCoreId() != ~0x0U && currentThread->getLockedCoreId() != currentCore->cpuId) {
			return 0;
		}

		if (!currentNode.hasRunnableThreads()) {
			return 1;
		}

		return 0;
	}

	extern "C" void loadNewThread() {
		CpuManager::getCurrentCore()->executionNode.loadNewThread();
	}

	extern "C" void finishScheduleSwitch() {
		CpuManager::getCurrentCore()->executionNode.finishScheduleSwitch();
	}

	extern "C" u128 scheduleEntry(const u64 oldRsp) {
		return CpuManager::getCurrentCore()->executionNode.schedule(oldRsp);
	}

	u128 ExecutionNode::schedule(const u64 oldRsp) {
		if (not this->isScheduling) {
			this->isScheduling = true;

			const auto current = __atomic_load_n(&CommonMain::schedulingCoresRemaining, __ATOMIC_RELAXED);

			__atomic_store_n(&CommonMain::schedulingCoresRemaining, current - 1, __ATOMIC_RELEASE);

			if (current == 1) {
				CommonMain::getInstance()->getPMM()->reclaimMemory();
			}
		}

		if (this->currentThread == nullptr) {
			CommonMain::getTerminal()->error("No current thread for EN: %lu", "Scheduler", CpuManager::getCurrentCore()->cpuId); // TODO: Use custom panic

			Asm::lhlt();
		}

		return this->saveOldThread(oldRsp);
	}

	u128 ExecutionNode::saveOldThread(const u64 oldRsp) {
		Scheduler *schedulerPtr = CommonMain::getInstance()->getScheduler();
		const bool prevIF = schedulerPtr->getSchedLock()->lock();
		this->setPendingSchedUnlock(prevIF);

		// Save the old thread state

		Thread *oldThread = this->currentThread;

		reinterpret_cast<ThreadContext *>(oldThread->getContext())->save();

		oldThread->setStackPointer(oldRsp);

		const u64 now = CommonMain::getInstance()->getClocks()->getMainClock()->getNs();

		if (oldThread != this->idleThread && oldThread->lastScheduledNs != 0 && oldThread->getState() == ThreadState::RUNNING) {
			oldThread->runTime += now - oldThread->lastScheduledNs;
			oldThread->recomputeDynPriority();
		}

		if (oldThread->getState() == ThreadState::TERMINATED) {
			schedulerPtr->awaitingKillThreadList.addEnd(oldThread);
		} else if (oldThread->getSleepNs() > 0) {
			oldThread->lastScheduledNs = now;

			if (!schedulerPtr->sleepingThreadList.contains(oldThread)) {
				schedulerPtr->sleepingThreadList.addEnd(oldThread);
			}
		} else if (oldThread->getState() == ThreadState::BLOCKED) {
			oldThread->lastScheduledNs = now;

			if (oldThread->getPendingWakeup()) {
				oldThread->setPendingWakeup(false);
				oldThread->setWaitingPort(0);
				oldThread->setSleepNs(0);
				oldThread->setState(ThreadState::RUNNING);

				schedulerPtr->enqueueThread(oldThread, true);
			} else if (!schedulerPtr->blockedThreadList.contains(oldThread)) {
				schedulerPtr->blockedThreadList.addEnd(oldThread);
			}
		} else if (oldThread != this->idleThread) {
			schedulerPtr->enqueueThread(oldThread);
		}

		if (reinterpret_cast<ThreadContext *>(oldThread->getContext())->threadTssIopb != nullptr) {
			this->oldThreadWasIopb = true;
		}

		// Get new thread

		const bool prevCoreIF = this->coreLock.lock();
		Thread *nextThread = this->getNextThread();
		this->coreLock.unlock(prevCoreIF);

		this->currentThread = nextThread;
		this->currentThread->setState(ThreadState::RUNNING);
		this->currentThread->lastScheduledNs = now;

		/*if (oldEntry != this->currentThread && oldEntry != nullptr) {
			//CommonMain::getTerminal()->debug("Switching from thread %lu to %lu", "Scheduler", oldEntry->value->getId(), this->currentThread->value->getId());
		}*/

		const u128 hi = static_cast<u128>(this->currentThread->getParent()->getProcessContextKernel()->pageMap.getAddr()) << 64;

		return hi | *this->currentThread->getStackPointer();
	}

	void ExecutionNode::finishScheduleSwitch() {
		if (!this->hasPendingSchedUnlock()) {
			return;
		}

		// TODO

		CommonMain::getInstance()->getScheduler()->getSchedLock()->unlock(this->consumePendingSchedUnlock());

		//this->consumePendingSchedUnlock();
		//CommonMain::getInstance()->getScheduler()->getSchedLock()->unlock(true);
	}

	void ExecutionNode::loadNewThread() {
		auto *ctx = reinterpret_cast<ThreadContext *>(this->currentThread->getContext());

		ctx->load();

		CpuCore *core = CpuManager::getCurrentCore();

		core->kernelStack = this->currentThread->getSyscallStackPointer();

		if (ctx->threadTssIopb != nullptr) {
			ctx->updateTssPtrs(this->currentThread->getKStackPointer());

			core->gdtManager->getGdt()->tssEntry = GdtTssEntry(ctx->threadTssIopb);

			TssManager::updateTss();
		} else {
			if (this->oldThreadWasIopb) {
				core->gdtManager->getGdt()->tssEntry = GdtTssEntry(core->tssManager->getTss());

				this->oldThreadWasIopb = false;

				TssManager::updateTss();
			}

			core->tssManager->getTss()->rsp[0] = this->currentThread->getKStackPointer();
		}
	}

	u64 ExecutionNode::getENThreadRsp() const {
		return CpuManager::getCurrentCore()->tssManager->getTss()->rsp[0];
	}

	u64 *Scheduler::createContext(Thread *thread, Process *process, const bool isUser, const u64 rip, const u64 rsp, const u64 userRsp) {
		const bool prevIF = Asm::intsEnabled();
		Asm::cli();

		const u64 currPageMap = Asm::readCr3();

		process->getProcessContextKernel()->pageMap.load();

		u64 newRsp = rsp;

		const u8 prid = process->pridAllocator.allocPRID();

		if (rsp == 0) {
			newRsp = reinterpret_cast<u64>(VirtualAllocator::alloc(process->getProcessContext(), threadCtxStackSize)) + threadCtxStackSize;
		}

		auto *context = reinterpret_cast<ThreadContext *>(VirtualAllocator::alloc(process->getProcessContext(), sizeof(ThreadContext)));

		*context = ThreadContext();

		context->init(process, newRsp, isUser, rsp == 0);

		context->prid = prid;

		thread->setStackPointer(newRsp);
		thread->setKStackPointer(newRsp);

		if (isUser) {
			thread->setSyscallStackPointer(reinterpret_cast<u64>(VirtualAllocator::alloc(process->getProcessContext(), threadCtxStackSize)) + threadCtxStackSize);

			u64 userStack = userRsp;

			if (userRsp == 0) {
				const u64 startAddr = VirtualAllocator::getProcessAllocStart() - ((threadUserStackSize + pageSize) * (prid + 1));

				const u64 startPage = alignDown<u64>(startAddr, pageSize);
				const u64 endPage = alignUp<u64>(startPage + threadUserStackSize, pageSize);

				// TODO: Prob wasting 1 page on the top addr
				for (u64 addr = startPage; addr < endPage; addr += pageSize) {
					const u64 *physPage = CommonMain::getInstance()->getPMM()->allocPages(1, false);

					if (physPage != nullptr) {
						process->getProcessContext()->pageMap.mapPage(addr, reinterpret_cast<u64>(physPage), process->getProcessContext()->pageFlags | 0b100, false, false);
					} else {
						CommonMain::getTerminal()->error("Failed to allocate physical memory for thread user ctx!", "Scheduler");
					}
				}

				CommonMain::getTerminal()->debug("User stack pointer: 0x%.16lx - 0x%.16lx, %lu", "Scheduler", startPage, startPage + threadUserStackSize, process->getProcessContext()->pageFlags | 0b100);

				context->userStackPointer = startPage;

				userStack = startPage + threadUserStackSize;

				setUserStackAsm(&userStack);
			}

			if (thread->is32Bit()) {
				setStackAsm(thread->getStackPointer(), reinterpret_cast<u64>(&threadTrampoline32), rip, userStack);
			} else {
				setStackAsm(thread->getStackPointer(), reinterpret_cast<u64>(&threadTrampoline64), rip, userStack);
			}
		} else {
			setStackAsm(thread->getStackPointer(), rip);
		}

		Asm::writeCr3(currPageMap);

		if (prevIF) {
			Asm::sti();
		}

		return reinterpret_cast<u64 *>(context);
	}

	ExecutionNode *Scheduler::getCurrentExecutionNode() {
		return &CpuManager::getCurrentCore()->executionNode;
	}

	void Scheduler::reaperThreadArch(const LinkedListEntry<Thread> *thread) {
		const u64 currPageMap = Asm::readCr3();

		thread->value->getParent()->removeThread(thread->value);

		thread->value->getParent()->getProcessContextKernel()->pageMap.load();

		delete thread->value;

		Asm::writeCr3(currPageMap);

		delete thread;
	}

	void Scheduler::reaperProcessArch(Process *process) {
		const u64 currPageMap = Asm::readCr3();

		this->processList.remove(process, false);

		process->getProcessContextKernel()->pageMap.load();

		delete process;

		Asm::writeCr3(currPageMap);
	}

	ExecutionNode *Scheduler::getCoreEN(const u64 cpuId) {
		auto *kernel = reinterpret_cast<Kernel *>(CommonMain::getInstance());
		const CpuManager *cpuManager = kernel->getCpuManager();

		CpuCore *destCore = nullptr;

		if (cpuManager->getBootstrapCpu()->cpuId == cpuId) {
			destCore = cpuManager->getBootstrapCpu();
		} else if (cpuManager->getCoreList() != nullptr && cpuManager->getCoreAmount() > 1) {
			for (u64 i = 0; i < cpuManager->getCoreAmount() - 1; i++) {
				if (cpuManager->getCoreList()[i].cpuCore.cpuId == cpuId) {
					destCore = &cpuManager->getCoreList()[i].cpuCore;
					break;
				}
			}
		}

		if (destCore == nullptr) {
			return nullptr;
		}

		return &destCore->executionNode;
	}
}

namespace kernel::x86_64::threading {
	using namespace utils;

	ThreadContext::~ThreadContext() {
		if (this->process != nullptr) {
			if (this->threadTssIopb != nullptr) {
				VirtualAllocator::free(this->process->getProcessContext(), reinterpret_cast<u64 *>(this->threadTssIopb));
				this->threadTssIopb = nullptr;
			}

			this->process->pridAllocator.freePRID(this->prid);

			VirtualAllocator::free(process->getProcessContext(), this->originalSimdSave);

			if (this->isUser) {
				const u64 startPage = alignDown<u64>(this->userStackPointer, pageSize);
				const u64 endPage = alignUp<u64>(this->userStackPointer + threadUserStackSize, pageSize);

				if (this->userStackPointer != 0) {
					for (u64 addr = startPage; addr < endPage; addr += pageSize) {
						CommonMain::getInstance()->getPMM()->freePagesCtx(process->getProcessContext(), reinterpret_cast<u64 *>(addr), 1);

						process->getProcessContext()->pageMap.unMapPage(addr);
					}
				}
			}
		}
	}

	void ThreadContext::init(Process *proc, const u64 stackPointer, const bool isUserspace, const bool ownsKernelStack) {
		this->isUser = isUserspace;
		this->userGsBase = 0;
		this->userFsBase = 0;
		this->process = proc;
		this->threadTssIopb = nullptr;
		this->originalStackPointer = stackPointer - threadCtxStackSize;
		this->ownsKernelStack = ownsKernelStack;
		this->process = proc;

		const u64 simdSaveAllocSize = CpuId::getXSaveSize() + 64;

		this->originalSimdSave = VirtualAllocator::alloc(proc->getProcessContext(), simdSaveAllocSize);
		memset(this->originalSimdSave, 0, simdSaveAllocSize);
		this->simdSave = reinterpret_cast<u64 *>(alignUp<u64>(reinterpret_cast<u64>(this->originalSimdSave), 64));

		CpuManager::initSimdContext(this->simdSave);
	}

	u64 *ThreadContext::getSimdSave() const {
		return this->simdSave;
	}

	void ThreadContext::save() {
		CpuManager::saveSimdContext(this->simdSave);

		this->userFsBase = Asm::rdmsr(Msrs::FSBAS);

		if (this->isUser) {
			this->userGsBase = Asm::rdmsr(Msrs::UGSBAS);
		}
	}

	void ThreadContext::load() const {
		CpuManager::loadSimdContext(this->simdSave);

		Asm::wrmsr(Msrs::FSBAS, this->userFsBase);

		if (this->isUser) {
			Asm::wrmsr(Msrs::UGSBAS, this->userGsBase);
		}
	}

	bool ThreadContext::isUserspace() const {
		return this->isUser;
	}

	void ThreadContext::updateTssPtrs(const u64 rsp0) {
		const Tss *tssPtrs = CpuManager::getCurrentCore()->tssManager->getTss();

		this->threadTssIopb->rsp[0] = rsp0;

		for (u8 i = 0; i < 7; i++) {
			this->threadTssIopb->ist[i] = tssPtrs->ist[i];
		}
	}
}

