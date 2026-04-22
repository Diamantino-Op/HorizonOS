#include "SchedulerX86.hpp"

#include "Main.hpp"
#include "CommonMain.hpp"
#include "Math.hpp"
#include "memory/MainMemory.hpp"

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
		const ThreadContext *threadContext = reinterpret_cast<ThreadContext *>(this->context);

		delete threadContext;
	}

	void Scheduler::initArch() {
		const Hpet *hpet = reinterpret_cast<Kernel *>(CommonMain::getInstance())->getHpet();

		if (hpet->getMaxTimers() == 0) {
			CommonMain::getTerminal()->error("Not enough hpet timers!", "Scheduler");

			return;
		}

		const u64 ticks = (10 * hpet->getFrequency()) / 1000;

		u32 gsi = reinterpret_cast<Kernel *>(CommonMain::getInstance())->getIOApicManager()->irqToIso(0xa); // 0x2c - irq 10

		if (gsi == 1'000'000) {
			CommonMain::getTerminal()->error("No gsi found!", "Scheduler");

			gsi = 0xc;
		} else {
			CommonMain::getTerminal()->debug("Gsi found: %lu", "Scheduler", gsi);
		}

		hpet->write(Hpet::getTimerRegister(0), ((gsi & ACPI_HPET_NUMBER_OF_COMPARATORS_MASK) << 9) | (1 << 2) | (1 << 3) | (1 << 6));
		hpet->write(Hpet::getComparatorRegister(0), hpet->read() + ticks);
		hpet->write(Hpet::getComparatorRegister(0), ticks);

		Interrupts::setHandler(0x2c, sleepTick, nullptr);

		Interrupts::unmask(0x2c);

		Interrupts::setHandler(0x21, intReSchedule, nullptr);
	}

	Thread *Scheduler::getCurrentThread() {
		return CpuManager::getCurrentCore()->executionNode.getCurrentThread()->value;
	}

	u32 Scheduler::intReSchedule(u64 *) {
		ExecutionNode::reSchedule();

		return 0;
	}

	void ExecutionNode::reSchedule() {
		switchContextAsm();
	}

	extern "C" u64 checkDisabled() {
		if (CpuManager::getCurrentCore()->executionNode.isDisabled()) {
			return 1;
		}

		if (Scheduler::getCurrentThread() == CpuManager::getCurrentCore()->executionNode.getIdleThread()->value) {
			return 0;
		}

		bool hasMoreThreads = false;

		for (const LinkedList<Thread>& currQueue : CommonMain::getInstance()->getScheduler()->queues) {
			if (currQueue.getSize() > 0) {
				hasMoreThreads = true;

				break;
			}
		}

		if (!hasMoreThreads && Scheduler::getCurrentThread()->getState() == ThreadState::RUNNING) {
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

		const LinkedListEntry<Thread> *oldEntry = this->currentThread;

		reinterpret_cast<ThreadContext *>(this->currentThread->value->getContext())->save();

		this->currentThread->value->setStackPointer(oldRsp);

		if (this->currentThread->value->getState() == ThreadState::TERMINATED) {
			// TODO: This might create nullptr if the reaper thread kills this while it is being used here
			schedulerPtr->awaitingKillThreadList.addEnd(this->currentThread);
		} else if (this->currentThread->value->getSleepNs() > 0) {
			if (!schedulerPtr->sleepingThreadList.contains(this->currentThread->value)) {
				schedulerPtr->sleepingThreadList.addEnd(this->currentThread);
			}
		} else if (this->currentThread->value->getState() == ThreadState::BLOCKED) {
			if (!schedulerPtr->blockedThreadList.contains(this->currentThread->value)) {
				schedulerPtr->blockedThreadList.addEnd(this->currentThread);
			}
		} else if (this->currentThread != this->idleThread) {
			schedulerPtr->queues[this->currentThread->value->getParent()->getPriority()].addEnd(this->currentThread);
		}

		// Get new thread

		if (schedulerPtr->readyThreadList.getSize() > 0) {
			this->currentThread = schedulerPtr->readyThreadList.removeFirstEntry();

			this->currentThread->value->setState(ThreadState::RUNNING);
		} else {
			LinkedListEntry<Thread> *selectedEntry = nullptr;

			for (LinkedList<Thread>& currQueue : schedulerPtr->queues) {
				if (currQueue.getSize() > 0) {
					selectedEntry = currQueue.removeFirstEntry();

					break;
				}
			}

			if (selectedEntry == nullptr) {
				selectedEntry = this->idleThread;
			}

			this->currentThread = selectedEntry;

			this->currentThread->next = nullptr;
			this->currentThread->prev = nullptr;
		}

		if (oldEntry != this->currentThread && oldEntry != nullptr) {
			CommonMain::getTerminal()->debug("Switching from thread %lu to %lu", "Scheduler", oldEntry->value->getId(), this->currentThread->value->getId());
		}

		const u128 hi = static_cast<u128>(this->currentThread->value->getParent()->getProcessContextKernel()->pageMap.getAddr()) << 64;

		return hi | *this->currentThread->value->getStackPointer();
	}

	void ExecutionNode::finishScheduleSwitch() {
		if (!this->hasPendingSchedUnlock()) {
			return;
		}

		CommonMain::getInstance()->getScheduler()->getSchedLock()->unlock(this->consumePendingSchedUnlock());
	}

	void ExecutionNode::loadNewThread() const {
		reinterpret_cast<ThreadContext *>(this->currentThread->value->getContext())->load();

		CpuManager::getCurrentCore()->tssManager->getTss()->rsp[0] = this->currentThread->value->getKStackPointer();

		Interrupts::sendEOI(0x21);
	}

	u64 ExecutionNode::getENThreadRsp() const {
		return CpuManager::getCurrentCore()->tssManager->getTss()->rsp[0];
	}

	u64 *Scheduler::createContext(Thread *thread, Process *process, const bool isUser, const u64 rip, const u64 rsp) {
		const u64 currPageMap = Asm::readCr3();

		process->getProcessContextKernel()->pageMap.load();

		u64 newRsp = rsp;

		const u8 prid = process->pridAllocator.allocPRID();

		if (rsp == 0) {
			newRsp = reinterpret_cast<u64>(VirtualAllocator::alloc(process->getProcessContext(), threadCtxStackSize)) + threadCtxStackSize;
		}

		auto *context = reinterpret_cast<ThreadContext *>(VirtualAllocator::alloc(process->getProcessContext(), sizeof(ThreadContext)));

		context->init(process, newRsp, isUser, rsp == 0);

		context->prid = prid;

		thread->setStackPointer(newRsp);
		thread->setKStackPointer(newRsp);

		if (isUser) {
			const u64 startAddr = VirtualAllocator::getProcessAllocStart() - ((threadCtxStackSize + pageSize) * (prid + 1));

			const u64 startPage = alignDown<u64>(startAddr, pageSize);
			const u64 endPage = alignUp<u64>(startPage + threadCtxStackSize, pageSize);

			for (u64 addr = startPage; addr < endPage; addr += pageSize) {
				const u64 *physPage = CommonMain::getInstance()->getPMM()->allocPages(1, false);

				if (physPage != nullptr) {
					process->getProcessContext()->pageMap.mapPage(addr, reinterpret_cast<u64>(physPage), process->getProcessContext()->pageFlags | 0b100, false, false);
				} else {
					CommonMain::getTerminal()->error("Failed to allocate physical memory for thread user ctx!", "Scheduler");
				}
			}

			CommonMain::getTerminal()->debug("User stack pointer: 0x%.16lx - 0x%.16lx, %lu", "Scheduler", startPage, startPage + threadCtxStackSize, process->getProcessContext()->pageFlags | 0b100);

			context->userStackPointer = startPage;

			u64 userStack = startPage + threadCtxStackSize;

			setUserStackAsm(&userStack);

			if (thread->is32Bit()) {
				setStackAsm(thread->getStackPointer(), reinterpret_cast<u64>(&threadTrampoline32), rip, userStack);
			} else {
				setStackAsm(thread->getStackPointer(), reinterpret_cast<u64>(&threadTrampoline64), rip, userStack);
			}
		} else {
			setStackAsm(thread->getStackPointer(), rip);
		}

		Asm::writeCr3(currPageMap);

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
}

namespace kernel::x86_64::threading {
	using namespace utils;

	ThreadContext::~ThreadContext() {
		if (this->process != nullptr) {
			this->process->pridAllocator.freePRID(this->prid);

			VirtualAllocator::free(process->getProcessContext(), this->originalSimdSave);

			if (this->ownsKernelStack) {
				VirtualAllocator::free(process->getProcessContext(), reinterpret_cast<u64 *>(this->originalStackPointer));
			}

			if (this->isUser) {
				const u64 startPage = alignDown<u64>(this->userStackPointer, pageSize);
				const u64 endPage = alignUp<u64>(this->userStackPointer + threadCtxStackSize, pageSize);

				for (u64 addr = startPage; addr < endPage; addr += pageSize) {
					CommonMain::getInstance()->getPMM()->freePagesCtx(process->getProcessContext(), reinterpret_cast<u64 *>(addr), 1);

					process->getProcessContext()->pageMap.unMapPage(addr);
				}
			}
		}
	}

	void ThreadContext::init(Process *process, const u64 stackPointer, const bool isUserspace, const bool ownsKernelStack) {
		this->isUser = isUserspace;
		this->userGsBase = 0;
		this->userFsBase = 0;
		this->process = process;
		this->originalStackPointer = stackPointer - threadCtxStackSize;
		this->ownsKernelStack = ownsKernelStack;
		this->process = process;

		this->originalSimdSave = VirtualAllocator::alloc(process->getProcessContext(), CpuId::getXSaveSize() + 64);
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
}

