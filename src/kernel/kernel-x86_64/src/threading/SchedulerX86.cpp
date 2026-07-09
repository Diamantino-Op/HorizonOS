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

	namespace {
		constexpr u64 threadContextMagic = 0x4852435458544354ULL; // HRCTXTCT

		[[noreturn]] void schedulerSwitchPanic(const char *reason, const Thread *thread, const u64 rsp) {
			auto *term = CommonMain::getTerminal();
			const u16 tid = thread != nullptr ? thread->getId() : 0xffff;
			const u16 pid = thread != nullptr && thread->getParent() != nullptr ? thread->getParent()->getId() : 0xffff;
			const u64 generation = thread != nullptr ? thread->getGeneration() : 0;
			const u32 state = thread != nullptr ? static_cast<u32>(thread->getState()) : 0xff;
			const u64 stackBase = thread != nullptr ? thread->getKernelStackBase() : 0;
			const u64 stackTop = thread != nullptr ? thread->getKernelStackTop() : 0;
			const u64 syscallStackBase = thread != nullptr ? thread->getSyscallStackBase() : 0;
			const u64 syscallStackTop = thread != nullptr ? thread->getSyscallStackPointer() : 0;
			const u64 queuedNode = thread != nullptr ? reinterpret_cast<u64>(thread->getQueuedExecutionNode()) : 0;
			const u32 queued = thread != nullptr ? static_cast<u32>(thread->isQueued()) : 0;

			term->printfBoth(true, "\033[0;31mScheduler switch panic: %s", reason);
			term->printfBoth(true, "\033[0;31m  cpu=%lu tid=%u pid=%u gen=%lu state=%u queued=%u queuedNode=0x%.16lx",
				CpuManager::getCurrentCore()->cpuId, tid, pid, generation, state, queued, queuedNode);
			term->printfBoth(true, "\033[0;31m  savedRsp=0x%.16lx stackBase=0x%.16lx stackTop=0x%.16lx thread=0x%.16lx",
				rsp, stackBase, stackTop, reinterpret_cast<u64>(thread));
			term->printfBoth(true, "\033[0;31m  syscallStackBase=0x%.16lx syscallStackTop=0x%.16lx",
				syscallStackBase, syscallStackTop);

			for (;;) {
				Asm::cli();
				Asm::hlt();
			}
		}

		[[noreturn]] void threadContextPanic(const char *reason, const Thread *thread, const ThreadContext *ctx) {
			auto *term = CommonMain::getTerminal();
			const u16 tid = thread != nullptr ? thread->getId() : 0xffff;
			const u16 pid = thread != nullptr && thread->getParent() != nullptr ? thread->getParent()->getId() : 0xffff;
			const u64 simd = ctx != nullptr ? reinterpret_cast<u64>(ctx->getSimdSave()) : 0;
			const u64 originalSimd = ctx != nullptr ? reinterpret_cast<u64>(ctx->getOriginalSimdSave()) : 0;
			const u64 simdSize = ctx != nullptr ? ctx->getSimdSaveAllocSize() : 0;

			term->printfBoth(true, "\033[0;31mThread context panic: %s", reason);
			term->printfBoth(true, "\033[0;31m  cpu=%lu tid=%u pid=%u thread=0x%.16lx ctx=0x%.16lx",
				CpuManager::getCurrentCore()->cpuId, tid, pid, reinterpret_cast<u64>(thread), reinterpret_cast<u64>(ctx));
			term->printfBoth(true, "\033[0;31m  simd=0x%.16lx originalSimd=0x%.16lx simdAllocSize=%lu KGS=0x%.16lx",
				simd, originalSimd, simdSize, Asm::rdmsr(KGSBAS));

			for (;;) {
				Asm::cli();
				Asm::hlt();
			}
		}
	}

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

	extern "C" u64 prepareScheduleSwitch() {
		u64 flags = 0;

		asm volatile(
			"pushfq\n"
			"cli\n"
			"popq %0"
			: "=r"(flags)
			:
			: "memory"
		);

		bool restoreIF = static_cast<bool>(flags & (1ULL << 9));
		CpuManager::getCurrentCore()->executionNode.consumeNextScheduleUnlockIF(restoreIF);

		return restoreIF;
	}

	extern "C" void loadNewThread() {
		CpuManager::getCurrentCore()->executionNode.loadNewThread();
	}

	extern "C" void finishScheduleSwitch() {
		CpuManager::getCurrentCore()->executionNode.finishScheduleSwitch();
	}

	extern "C" void kernelThreadReturned() {
		Scheduler *scheduler = CommonMain::getInstance() != nullptr ? CommonMain::getInstance()->getScheduler() : nullptr;
		Thread *thread = Scheduler::getCurrentThread();

		if (scheduler != nullptr && thread != nullptr) {
			scheduler->killThread(thread);
		}

		for (;;) {
			Asm::cli();
			Asm::hlt();
		}
	}

	extern "C" u128 scheduleEntry(const u64 oldRsp) {
		return CpuManager::getCurrentCore()->executionNode.schedule(oldRsp);
	}

	u128 ExecutionNode::schedule(const u64 oldRsp) {
		if (not this->isScheduling) {
			this->isScheduling = true;

			const auto current = __atomic_fetch_sub(&CommonMain::schedulingCoresRemaining, 1, __ATOMIC_ACQ_REL);

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
		const bool oldThreadIsUnstartedIdle = oldThread == this->idleThread && !this->idleThreadStarted;

		reinterpret_cast<ThreadContext *>(oldThread->getContext())->save();

		if (!oldThreadIsUnstartedIdle) {
			oldThread->setStackPointer(oldRsp);

			if (!oldThread->isSavedStackPointerValid()) {
				schedulerSwitchPanic("invalid old thread saved rsp", oldThread, oldRsp);
			}
		}

		const u64 now = CommonMain::getInstance()->getClocks()->getMainClock()->getNs();

		if (!oldThreadIsUnstartedIdle && oldThread != this->idleThread && oldThread->lastScheduledNs != 0 && oldThread->getState() == ThreadState::RUNNING) {
			oldThread->runTime += now - oldThread->lastScheduledNs;
			oldThread->recomputeDynPriority();
		}

		if (!oldThreadIsUnstartedIdle && oldThread->getState() == ThreadState::TERMINATED) {
			if (oldThread->isKillCleanupInProgress()) {
				oldThread->setReapPending(true);
			} else {
				schedulerPtr->awaitingKillThreadList.addEnd(oldThread, false);
			}
		} else if (!oldThreadIsUnstartedIdle && oldThread->getSleepNs() > 0) {
			oldThread->lastScheduledNs = now;

			if (!schedulerPtr->sleepingThreadList.contains(oldThread)) {
				schedulerPtr->sleepingThreadList.addEnd(oldThread, false);
			}
		} else if (!oldThreadIsUnstartedIdle && oldThread->getState() == ThreadState::BLOCKED) {
			oldThread->lastScheduledNs = now;

			if (oldThread->getPendingWakeup()) {
				oldThread->setPendingWakeup(false);
				oldThread->setWaitingPort(0);
				oldThread->setSleepNs(0);
				oldThread->setState(ThreadState::RUNNING);

				schedulerPtr->enqueueThread(oldThread, true);
			} else if (!schedulerPtr->blockedThreadList.contains(oldThread)) {
				schedulerPtr->blockedThreadList.addEnd(oldThread, false);
			}
		} else if (!oldThreadIsUnstartedIdle && oldThread != this->idleThread) {
			schedulerPtr->enqueueThread(oldThread);
		}

		if (reinterpret_cast<ThreadContext *>(oldThread->getContext())->threadTssIopb != nullptr) {
			this->oldThreadWasIopb = true;
		}

		// Get new thread

		const bool prevCoreIF = this->coreLock.lock();
		Thread *nextThread = this->getNextThread();
		this->coreLock.unlock(prevCoreIF);

		if (nextThread == nullptr) {
			schedulerSwitchPanic("no next thread", nullptr, 0);
		}

		const u64 nextRsp = *nextThread->getStackPointer();

		if (nextThread->getContext() == nullptr || nextThread->getParent() == nullptr || nextThread->getParent()->getProcessContextKernel() == nullptr) {
			schedulerSwitchPanic("invalid next thread context", nextThread, nextRsp);
		}

		if (!nextThread->isSavedStackPointerValid()) {
			schedulerSwitchPanic("invalid next thread saved rsp", nextThread, nextRsp);
		}

		this->currentThread = nextThread;
		this->currentThread->setState(ThreadState::RUNNING);
		this->currentThread->lastScheduledNs = now;

		if (this->currentThread == this->idleThread) {
			this->idleThreadStarted = true;
		}

		/*if (oldThread != this->currentThread) {
			CommonMain::getTerminal()->printfBoth(true, "Switching from thread %lu to %lu", oldThread->getId(), this->currentThread->getId());
		}

		if (this->currentThread->getId() == this->prevPrevThreadId) {
			if (this->prevPrevCount < 10) {
				this->prevPrevCount++;
			} else {
				Scheduler::debugDump();

				for (;;) {
					Asm::cli();
					Asm::hlt();
				}
			}
		}

		this->prevPrevThreadId = oldThread->getId();*/

		const u128 hi = static_cast<u128>(this->currentThread->getParent()->getProcessContextKernel()->pageMap.getAddr()) << 64;

		return hi | nextRsp;
	}

	void ExecutionNode::finishScheduleSwitch() {
		if (!this->hasPendingSchedUnlock()) {
			return;
		}

		// TODO

		this->consumePendingSchedUnlock();
		CommonMain::getInstance()->getScheduler()->getSchedLock()->unlockNoSti();

		//this->consumePendingSchedUnlock();
		//CommonMain::getInstance()->getScheduler()->getSchedLock()->unlock(true);
	}

	void ExecutionNode::loadNewThread() {
		auto *ctx = reinterpret_cast<ThreadContext *>(this->currentThread->getContext());

		if (ctx == nullptr || !ctx->isValid()) {
			threadContextPanic("invalid context before load", this->currentThread, ctx);
		}

		ctx->load();

		CpuCore *core = CpuManager::getCurrentCore();

		core->kernelStack = this->currentThread->getSyscallStackPointer();

		if (ctx->threadTssIopb != nullptr) {
			ctx->updateTssPtrs(this->currentThread->getKStackPointer());

			core->gdtManager->getGdt()->tssEntry = GdtTssEntry(ctx->threadTssIopb);
			core->gdtManager->getGdt()->tssEntry.clearBusy();

			TssManager::updateTss();
		} else {
			if (this->oldThreadWasIopb) {
				core->gdtManager->getGdt()->tssEntry = GdtTssEntry(core->tssManager->getTss());
				core->gdtManager->getGdt()->tssEntry.clearBusy();

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

		//const u64 currPageMap = Asm::readCr3();

		//process->getProcessContextKernel()->pageMap.load();

		AllocContext *ctx = CommonMain::getInstance()->getKernelAllocContext();

		u64 newRsp = rsp;
		u64 *kernelStack = nullptr;

		if (rsp == 0) {
			kernelStack = VirtualAllocator::alloc(ctx, threadCtxStackSize);

			if (kernelStack == nullptr) {
				if (prevIF) {
					Asm::sti();
				}

				return nullptr;
			}

			newRsp = reinterpret_cast<u64>(kernelStack) + threadCtxStackSize;
		}

		const u64 kernelStackBase = kernelStack != nullptr ? reinterpret_cast<u64>(kernelStack) : newRsp - threadCtxStackSize;

		auto *context = reinterpret_cast<ThreadContext *>(VirtualAllocator::alloc(ctx, sizeof(ThreadContext)));

		if (context == nullptr) {
			if (kernelStack != nullptr) {
				VirtualAllocator::free(ctx, kernelStack);
			}

			if (prevIF) {
				Asm::sti();
			}

			return nullptr;
		}

		*context = ThreadContext();

		if (!context->init(process, newRsp, isUser)) {
			VirtualAllocator::free(ctx, reinterpret_cast<u64 *>(context));

			if (kernelStack != nullptr) {
				VirtualAllocator::free(ctx, kernelStack);
			}

			if (prevIF) {
				Asm::sti();
			}

			return nullptr;
		}

		const u8 prid = process->pridAllocator.allocPRID();

		if (prid >= maxProcThreads) {
			context->~ThreadContext();
			VirtualAllocator::free(ctx, reinterpret_cast<u64 *>(context));

			if (kernelStack != nullptr) {
				VirtualAllocator::free(ctx, kernelStack);
			}

			if (prevIF) {
				Asm::sti();
			}

			return nullptr;
		}

		context->prid = prid;

		thread->setStackPointer(newRsp);
		thread->setKernelStackBounds(kernelStackBase, newRsp);
		thread->setKernelStackOwned(kernelStack != nullptr);

		if (isUser) {
			u64 *syscallStack = VirtualAllocator::alloc(ctx, threadCtxStackSize);

			if (syscallStack == nullptr) {
				context->~ThreadContext();
				VirtualAllocator::free(ctx, reinterpret_cast<u64 *>(context));

				if (kernelStack != nullptr) {
					thread->setKernelStackOwned(false);
					thread->setKStackPointer(0);
					thread->setStackPointer(0);
					VirtualAllocator::free(ctx, kernelStack);
				}

				if (prevIF) {
					Asm::sti();
				}

				return nullptr;
			}

			thread->setSyscallStackBounds(reinterpret_cast<u64>(syscallStack), reinterpret_cast<u64>(syscallStack) + threadCtxStackSize);

			u64 userStack = userRsp;

			if (userRsp == 0) {
				const u64 currPageMap = Asm::readCr3();

				process->getProcessContextKernel()->pageMap.load();

				const u64 startAddr = VirtualAllocator::getProcessAllocStart() - ((threadUserStackSize + pageSize) * (prid + 1));

				const u64 startPage = alignDown<u64>(startAddr, pageSize);
				const u64 endPage = alignUp<u64>(startPage + threadUserStackSize, pageSize);
				u64 mappedEnd = startPage;

				// TODO: Prob wasting 1 page on the top addr
				for (u64 addr = startPage; addr < endPage; addr += pageSize) {
					const u64 *physPage = CommonMain::getInstance()->getPMM()->allocPages(1, false);

					if (physPage != nullptr) {
						process->getProcessContext()->pageMap.mapPage(addr, reinterpret_cast<u64>(physPage), process->getProcessContext()->pageFlags | 0b100, false, false);
						mappedEnd = addr + pageSize;
					} else {
						CommonMain::getTerminal()->error("Failed to allocate physical memory for thread user ctx!", "Scheduler");

						for (u64 mappedAddr = startPage; mappedAddr < mappedEnd; mappedAddr += pageSize) {
							process->getProcessContext()->pageMap.unMapPage(mappedAddr, true);
						}

						Asm::writeCr3(currPageMap);

						thread->setSyscallStackBounds(0, 0);
						VirtualAllocator::free(ctx, syscallStack);

						context->~ThreadContext();
						VirtualAllocator::free(ctx, reinterpret_cast<u64 *>(context));

						if (kernelStack != nullptr) {
							thread->setKernelStackOwned(false);
							VirtualAllocator::free(ctx, kernelStack);
						}

						if (prevIF) {
							Asm::sti();
						}

						return nullptr;
					}
				}

				CommonMain::getTerminal()->debug("User stack pointer: 0x%.16lx - 0x%.16lx, %lu", "Scheduler", startPage, startPage + threadUserStackSize, process->getProcessContext()->pageFlags | 0b100);

				context->userStackPointer = startPage;

				userStack = startPage + threadUserStackSize;

				setUserStackAsm(&userStack);

				Asm::writeCr3(currPageMap);
			}

			if (thread->is32Bit()) {
				setStackAsm(thread->getStackPointer(), reinterpret_cast<u64>(&threadTrampoline32), rip, userStack);
			} else {
				setStackAsm(thread->getStackPointer(), reinterpret_cast<u64>(&threadTrampoline64), rip, userStack);
			}
		} else {
			setStackAsm(thread->getStackPointer(), reinterpret_cast<u64>(&kernelThreadTrampoline), rip);
		}

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

		this->processList.remove(process, false, false);

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
				VirtualAllocator::free(CommonMain::getInstance()->getKernelAllocContext(), reinterpret_cast<u64 *>(this->threadTssIopb));
				this->threadTssIopb = nullptr;
			}

			this->process->pridAllocator.freePRID(this->prid);

			VirtualAllocator::free(CommonMain::getInstance()->getKernelAllocContext(), this->originalSimdSave);

			if (this->isUser) {
				const u64 startPage = alignDown<u64>(this->userStackPointer, pageSize);
				const u64 endPage = alignUp<u64>(this->userStackPointer + threadUserStackSize, pageSize);

				if (this->userStackPointer != 0) {
					for (u64 addr = startPage; addr < endPage; addr += pageSize) {
						process->getProcessContext()->pageMap.unMapPage(addr, true);
					}
				}
			}
		}

		this->magic = 0;
	}

	bool ThreadContext::init(Process *proc, const u64 stackPointer, const bool isUserspace) {
		this->magic = threadContextMagic;
		this->isUser = isUserspace;
		this->userGsBase = 0;
		this->userFsBase = 0;
		this->process = proc;
		this->threadTssIopb = nullptr;
		this->originalStackPointer = stackPointer - threadCtxStackSize;
		this->process = proc;

		this->simdSaveAllocSize = CpuId::getXSaveSize() + 64;

		this->originalSimdSave = VirtualAllocator::alloc(CommonMain::getInstance()->getKernelAllocContext(), this->simdSaveAllocSize);

		if (this->originalSimdSave == nullptr) {
			return false;
		}

		memset(this->originalSimdSave, 0, this->simdSaveAllocSize);
		this->simdSave = reinterpret_cast<u64 *>(alignUp<u64>(reinterpret_cast<u64>(this->originalSimdSave), 64));

		CpuManager::initSimdContext(this->simdSave);

		return true;
	}

	u64 *ThreadContext::getSimdSave() const {
		return this->simdSave;
	}

	u64 *ThreadContext::getOriginalSimdSave() const {
		return this->originalSimdSave;
	}

	u64 ThreadContext::getSimdSaveAllocSize() const {
		return this->simdSaveAllocSize;
	}

	bool ThreadContext::isValid() const {
		if (this->magic != threadContextMagic || this->originalSimdSave == nullptr || this->simdSave == nullptr || this->simdSaveAllocSize < 512) {
			return false;
		}

		const u64 original = reinterpret_cast<u64>(this->originalSimdSave);
		const u64 simd = reinterpret_cast<u64>(this->simdSave);

		return (simd & 0x3fU) == 0 && simd >= original && simd + 512 <= original + this->simdSaveAllocSize;
	}

	void ThreadContext::save() {
		if (!this->isValid()) {
			threadContextPanic("invalid context before save", Scheduler::getCurrentThread(), this);
		}

		u64 *simd = this->simdSave;
		u64 *originalSimd = this->originalSimdSave;
		const u64 allocSize = this->simdSaveAllocSize;

		CpuManager::saveSimdContextChecked(simd, originalSimd, allocSize);

		this->userFsBase = Asm::rdmsr(Msrs::FSBAS);

		if (this->isUser) {
			this->userGsBase = Asm::rdmsr(Msrs::UGSBAS);
		}
	}

	void ThreadContext::load() const {
		if (!this->isValid()) {
			threadContextPanic("invalid context before simd restore", Scheduler::getCurrentThread(), this);
		}

		u64 *simd = this->simdSave;
		u64 *originalSimd = this->originalSimdSave;
		const u64 allocSize = this->simdSaveAllocSize;

		CpuManager::loadSimdContextChecked(simd, originalSimd, allocSize);

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

