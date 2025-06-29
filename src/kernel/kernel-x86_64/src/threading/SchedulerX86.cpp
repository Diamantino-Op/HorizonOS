#include "SchedulerX86.hpp"

#include "Main.hpp"
#include "CommonMain.hpp"
#include "Math.hpp"
#include "memory/MainMemory.hpp"
#include "threading/Scheduler.hpp"

#include "hal/Cpu.hpp"
#include "hal/Hpet.hpp"
#include "utils/Asm.hpp"
#include "utils/CpuId.hpp"

namespace kernel::common::threading {
	using namespace x86_64;
	using namespace x86_64::threading;
	using namespace x86_64::utils;

	void idleThread() {
		Asm::lhlt();
	}

	void Scheduler::initArch() {
		const Hpet *hpet = reinterpret_cast<Kernel *>(CommonMain::getInstance())->getHpet();

		if (hpet->getMaxTimers() == 0) {
			CommonMain::getTerminal()->error("Not enough hpet timers!", "Scheduler");

			return;
		}

		const u64 ticks = (10 * hpet->getFrequency()) / 1000;

		u32 gsi = reinterpret_cast<Kernel *>(CommonMain::getInstance())->getIOApicManager()->irqToIso(0xa); // 0x2a - irq 10

		if (gsi == 1'000'000) {
			CommonMain::getTerminal()->error("No gsi found!", "Scheduler");

			gsi = 0xa;
		} else {
			CommonMain::getTerminal()->debug("Gsi found: %lu", "Scheduler", gsi);
		}

		hpet->write(Hpet::getTimerRegister(0), ((gsi & ACPI_HPET_NUMBER_OF_COMPARATORS_MASK) << 9) | (1 << 2) | (1 << 3) | (1 << 6));
		hpet->write(Hpet::getComparatorRegister(0), hpet->read() + ticks);
		hpet->write(Hpet::getComparatorRegister(0), ticks);

		Interrupts::setHandler(0x2a, sleepTick, nullptr);

		Interrupts::unmask(0x2a);
	}

	void ExecutionNode::initArch() {
		const u64 intNum = reinterpret_cast<Kernel *>(CommonMain::getInstance())->getIOApicManager()->getMaxRange() + 0x20;

		Interrupts::setHandler(intNum, scheduleTick, nullptr);

		//Interrupts::unmask(intNum);
	}

	u32 ExecutionNode::scheduleTick(u64 *) {
		CpuManager::getCurrentCore()->executionNode.schedule();

		return 0;
	}

	void ExecutionNode::schedule() {
		Asm::cli();

		Scheduler *schedulerPtr = CommonMain::getInstance()->getScheduler();

		if (this->isDisabledFlag) {
			Asm::sti();

			return;
		}

		schedulerPtr->getSchedLock()->lock();

		if (this->currentThread == nullptr) {
			CommonMain::getTerminal()->error("No current thread for EN: %lu", "Scheduler", CpuManager::getCurrentCore()->cpuId); // TODO: Use custom panic

			Asm::lhlt();
		}

		switchThreads();
	}

	void ExecutionNode::switchThreads() {
		Scheduler *schedulerPtr = CommonMain::getInstance()->getScheduler();

		if (ThreadListEntry *lastEntry = schedulerPtr->lastQueueEntry[this->currentThread->thread->getParent()->getPriority()]; lastEntry != nullptr) {
			this->currentThread->prev = lastEntry;
			this->currentThread->next = nullptr;

			if (this->currentThread->prev != nullptr) {
				this->currentThread->prev->next = this->currentThread;
			}

			schedulerPtr->lastQueueEntry[this->currentThread->thread->getParent()->getPriority()] = this->currentThread;
		} else {
			schedulerPtr->queues[this->currentThread->thread->getParent()->getPriority()] = this->currentThread;
			schedulerPtr->lastQueueEntry[this->currentThread->thread->getParent()->getPriority()] = this->currentThread;
		}

		const ThreadListEntry *oldEntry = this->currentThread;

		if (schedulerPtr->readyThreadList != nullptr) {
			this->currentThread = schedulerPtr->readyThreadList;

			if (schedulerPtr->readyThreadList->next != nullptr) {
				schedulerPtr->readyThreadList->next->prev = nullptr;
			}

			schedulerPtr->readyThreadList = schedulerPtr->readyThreadList->next;

			this->currentThread->next = nullptr;
			this->currentThread->prev = nullptr;

			this->currentThread->thread->setState(ThreadState::RUNNING);

			// TODO: Make trampoline for user threads
		} else {
			ThreadListEntry *selectedEntry = nullptr;

			for (const auto currQueue : schedulerPtr->queues) {
				ThreadListEntry *tmpEntry = currQueue;

				while (tmpEntry != nullptr and tmpEntry->next != nullptr and tmpEntry->thread->getState() != ThreadState::RUNNING) {
					tmpEntry = tmpEntry->next;
				}

				if (tmpEntry != nullptr and tmpEntry->thread->getState() == ThreadState::RUNNING) {
					selectedEntry = tmpEntry;

					break;
				}
			}

			if (selectedEntry == nullptr) {
				CommonMain::getTerminal()->error("No thread to switch to for EN: %lu", "Scheduler", CpuManager::getCurrentCore()->cpuId); // TODO: Use custom panic

				Asm::lhlt();
			}

			schedulerPtr->queues[selectedEntry->thread->getParent()->getPriority()] = selectedEntry->next;

			if (schedulerPtr->lastQueueEntry[selectedEntry->thread->getParent()->getPriority()] == selectedEntry) {
				schedulerPtr->lastQueueEntry[selectedEntry->thread->getParent()->getPriority()] = selectedEntry->prev;
			}

			if (selectedEntry->next != nullptr) {
				selectedEntry->next->prev = nullptr;
			}

			this->currentThread = selectedEntry;

			this->currentThread->next = nullptr;
			this->currentThread->prev = nullptr;
		}

		if (oldEntry != this->currentThread) {
			CommonMain::getTerminal()->debug("Switching from thread %lu to %lu", "Scheduler", oldEntry->thread->getId(), this->currentThread->thread->getId());
		}

		Asm::wrmsr(Msrs::FSBAS, reinterpret_cast<u64>(this->currentThread->thread));

		if (reinterpret_cast<u64>(reinterpret_cast<ThreadContext *>(this->currentThread->thread->getContext())->getSimdSave()) < pageSize) {
			CommonMain::getTerminal()->error("NewEntry simdSave is null!", "Scheduler"); // TODO: Use custom panic

			Asm::lhlt();
		}

		if (reinterpret_cast<u64>(reinterpret_cast<ThreadContext *>(oldEntry->thread->getContext())->getSimdSave()) < pageSize) {
			CommonMain::getTerminal()->error("OldEntry simdSave is null!", "Scheduler"); // TODO: Use custom panic

			Asm::lhlt();
		}

		switchContext(oldEntry->thread->getContext(), this->currentThread->thread->getContext());
	}

	// Old Ctx = Current Thread, New Ctx = New Thread
	void ExecutionNode::switchContext(u64 *oldCtx, u64 *newCtx) const {
		auto *oldCtxConv = reinterpret_cast<ThreadContext *>(oldCtx);
		auto *newCtxConv = reinterpret_cast<ThreadContext *>(newCtx);

		oldCtxConv->save();

		this->currentThread->thread->getParent()->getProcessContext()->pageMap.load();

		newCtxConv->load();

		Interrupts::sendEOI(0x2a);

		CommonMain::getInstance()->getScheduler()->getSchedLock()->unlock();

		Asm::sti();

		switchContextAsm(oldCtxConv->getStackPointer(), newCtxConv->getStackPointer());
	}

	u64 *Scheduler::createContextArch(const bool isUser, const u64 rip, const u64 rsp) {
		auto *context = new ThreadContext(rsp, isUser);

		setStackAsm(context->getStackPointer(), rip);

		return reinterpret_cast<u64 *>(context);
	}

	ExecutionNode *Scheduler::getCurrentExecutionNode() const {
		return &CpuManager::getCurrentCore()->executionNode;
	}
}

namespace kernel::x86_64::threading {
	using namespace utils;

	ThreadContext::ThreadContext(const u64 stackPointer, const bool isUserspace) : isUser(isUserspace), originalStackPointer(stackPointer - threadCtxStackSize), stackPointer(stackPointer) {
		this->originalSimdSave = static_cast<u64 *>(malloc(CpuId::getXSaveSize() + 64));
		this->simdSave = reinterpret_cast<u64 *>(alignUp<u64>(reinterpret_cast<u64>(this->originalSimdSave), 64));

		CpuManager::initSimdContext(this->simdSave);
	}

	ThreadContext::~ThreadContext() {
		free(this->originalSimdSave);
		free(reinterpret_cast<u64 *>(this->originalStackPointer));
	}

	u64 *ThreadContext::getStackPointer() {
		return &this->stackPointer;
	}

	void ThreadContext::setStackPointer(const u64 stackPtr) {
		this->stackPointer = stackPtr;
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