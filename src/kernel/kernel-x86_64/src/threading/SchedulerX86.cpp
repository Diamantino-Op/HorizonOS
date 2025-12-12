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

	void idleThread() {
		for (;;) {
			CommonMain::getTerminal()->debug("Idle Tick", "Idle");

			asm volatile ("pause" ::: "memory");
		}
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
	}

	Thread *Scheduler::getCurrentThread() {
		return reinterpret_cast<Thread *>(Asm::rdmsr(Msrs::FSBAS));
	}

	void ExecutionNode::reSchedule() {
		asm inline("int %0" :: "i"(0x21));
	}

	extern "C" u64 getCurrThreadRsp() {
		return *CpuManager::getCurrentCore()->executionNode.getCurrentThread()->value->getStackPointer();
	}

	extern "C" u64 scheduleEntry(const u64 oldRsp) {
		return CpuManager::getCurrentCore()->executionNode.schedule(oldRsp);
	}

	u64 ExecutionNode::schedule(const u64 oldRsp) {
		Scheduler *schedulerPtr = CommonMain::getInstance()->getScheduler();

		if (this->isDisabledFlag) {
			return oldRsp;
		}

		this->prevIF = schedulerPtr->getSchedLock()->lock();

		if (this->currentThread == nullptr) {
			CommonMain::getTerminal()->error("No current thread for EN: %lu", "Scheduler", CpuManager::getCurrentCore()->cpuId); // TODO: Use custom panic

			Asm::lhlt();
		}

		return switchThreads(oldRsp);
	}

	u64 ExecutionNode::switchThreads(const u64 oldRsp) {
		Scheduler *schedulerPtr = CommonMain::getInstance()->getScheduler();

		// Save the old thread state

		if (this->currentThread->value->getState() == ThreadState::TERMINATED) {
			// TODO: This might create nullptr if the reaper thread kills this while it is being used here
			schedulerPtr->awaitingKillThreadList.addEnd(this->currentThread);
		} else if (this->currentThread->value->getSleepNs() > 0) {
			schedulerPtr->sleepingThreadList.addEnd(this->currentThread);
		} else if (this->currentThread->value->getState() == ThreadState::BLOCKED) {
			schedulerPtr->blockedThreadList.addEnd(this->currentThread);
		} else {
			schedulerPtr->queues[this->currentThread->value->getParent()->getPriority()].addEnd(this->currentThread);
		}

		const LinkedListEntry<Thread> *oldEntry = this->currentThread;

		reinterpret_cast<ThreadContext *>(this->currentThread->value->getContext())->save();

		this->currentThread->value->setStackPointer(oldRsp);

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
				CommonMain::getTerminal()->error("No thread to switch to for EN: %lu", "Scheduler", CpuManager::getCurrentCore()->cpuId); // TODO: Use custom panic

				Asm::lhlt();
			}

			this->currentThread = selectedEntry;

			this->currentThread->next = nullptr;
			this->currentThread->prev = nullptr;
		}

		this->currentThread->value->getParent()->getProcessContextKernel()->pageMap.load();

		if (oldEntry != this->currentThread) {
			CommonMain::getTerminal()->debug("Switching from thread %lu to %lu", "Scheduler", oldEntry->value->getId(), this->currentThread->value->getId());
		}

		Asm::wrmsr(Msrs::FSBAS, reinterpret_cast<u64>(this->currentThread->value));

		CommonMain::getTerminal()->debug("Switch Old RSP: 0x%.16lx, New RSP: 0x%.16lx", "Scheduler", oldRsp, *this->currentThread->value->getStackPointer());

		reinterpret_cast<ThreadContext *>(this->currentThread->value->getContext())->load();

		CpuManager::getCurrentCore()->tssManager->getTss()->rsp[0] = this->currentThread->value->getKStackPointer();

		CommonMain::getInstance()->getScheduler()->getSchedLock()->unlock(this->prevIF);

		return *this->currentThread->value->getStackPointer();
	}

	u64 ExecutionNode::getENThreadRsp() const {
		return CpuManager::getCurrentCore()->tssManager->getTss()->rsp[0];
	}

	u64 *Scheduler::createContext(Thread *thread, Process *process, const bool isUser, const u64 rip, const u64 rsp) {
		const u64 currPageMap = Asm::readCr3();

		process->getProcessContextKernel()->pageMap.load();

		u64 newRsp = rsp;

		if (rsp == 0) {
			newRsp = reinterpret_cast<u64>(VirtualAllocator::alloc(process->getProcessContext(), threadCtxStackSize)) + threadCtxStackSize;
		}

		auto *context = reinterpret_cast<ThreadContext *>(VirtualAllocator::alloc(process->getProcessContext(), sizeof(ThreadContext)));

		context->init(process, newRsp, isUser);

		thread->setStackPointer(newRsp);
		thread->setKStackPointer(newRsp);

		if (isUser) {
			const u64 userStack = reinterpret_cast<u64>(VirtualAllocator::alloc(process->getProcessContext(), threadCtxStackSize)) + threadCtxStackSize;

			if (thread->is32Bit()) {
				setStackAsm(thread->getStackPointer(), reinterpret_cast<u64>(threadTrampoline32), rip, userStack);
			} else {
				setStackAsm(thread->getStackPointer(), reinterpret_cast<u64>(threadTrampoline64), rip, userStack);
			}
		} else {
			setStackAsm(thread->getStackPointer(), rip);
		}

		Asm::writeCr3(currPageMap);

		return reinterpret_cast<u64 *>(context);
	}

	void Scheduler::sendSleepEOI() {
		Interrupts::sendEOI(0x2b);
	}

	ExecutionNode *Scheduler::getCurrentExecutionNode() {
		return &CpuManager::getCurrentCore()->executionNode;
	}
}

namespace kernel::x86_64::threading {
	using namespace utils;

	ThreadContext::~ThreadContext() {
		if (this->process != nullptr) {
			VirtualAllocator::free(process->getProcessContext(), this->originalSimdSave);
			VirtualAllocator::free(process->getProcessContext(), &this->originalStackPointer);
		}
	}

	void ThreadContext::init(Process *process, const u64 stackPointer, const bool isUserspace) {
		this->isUser = isUserspace;
		this->process = process;
		this->originalStackPointer = stackPointer - threadCtxStackSize;
		this->process = process;

		this->originalSimdSave = VirtualAllocator::alloc(process->getProcessContext(), CpuId::getXSaveSize() + 64);
		this->simdSave = reinterpret_cast<u64 *>(alignUp<u64>(reinterpret_cast<u64>(this->originalSimdSave), 64));

		CpuManager::initSimdContext(this->simdSave);
	}

	u64 *ThreadContext::getSimdSave() const {
		return this->simdSave;
	}

	void ThreadContext::save() const {
		CpuManager::saveSimdContext(this->simdSave);
	}

	void ThreadContext::load() const {
		CpuManager::loadSimdContext(this->simdSave);
	}

	bool ThreadContext::isUserspace() const {
		return this->isUser;
	}
}