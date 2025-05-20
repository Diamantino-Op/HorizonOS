#include "SchedulerX86.hpp"

#include "CommonMain.hpp"
#include "memory/MainMemory.hpp"
#include "threading/Scheduler.hpp"

#include "hal/Cpu.hpp"
#include "utils/Asm.hpp"
#include "utils/CpuId.hpp"

#include <Math.hpp>

namespace kernel::common::threading {
	using namespace x86_64::threading;
	using namespace x86_64::utils;

	void idleThread() {
		Asm::lhlt();
	}

	void ExecutionNode::schedule() {
		Asm::cli();

		CommonMain::getInstance()->getKernelAllocContext()->pageMap.load(); // TODO: Prob VERY bad for performance

		for (Scheduler *schedulerPtr = CommonMain::getInstance()->getScheduler(); auto currQueue : schedulerPtr->queues) {
			while (currQueue != nullptr) {
				if (currQueue->thread->getSleepTicks() > 0) {
					currQueue->thread->setSleepTicks(currQueue->thread->getSleepTicks() - 1);

					if (currQueue->thread->getSleepTicks() == 0) {
						currQueue->thread->setState(ThreadState::READY);
					}
				}

				currQueue = currQueue->next;
			}
		}

		if (this->currentThread == nullptr) {
			CommonMain::getTerminal()->error("No current thread for EN: %lu", "Scheduler", CpuManager::getCurrentCore()->cpuId); // TODO: Use custom panic

			Asm::lhlt();
		}

		if (this->currentThread->thread->getState() == ThreadState::RUNNING) {
			if (this->remainingTicks > 0) {
				this->remainingTicks--;

				this->currentThread->thread->getParent()->getProcessContext()->pageMap.load(); // TODO: Prob VERY bad for performance

				Asm::sti();

				return;
			}

			switchThreads();
		} else {
			switchThreads();
		}

		Asm::sti();
	}

	void ExecutionNode::switchThreads() {
		Scheduler *schedulerPtr = CommonMain::getInstance()->getScheduler();

		if (this->currentThread != nullptr) {
			if (const ThreadListEntry *currEntry = schedulerPtr->queues[currentThread->thread->getParent()->getPriority()]; currEntry != nullptr) {
				ThreadListEntry *lastEntry = schedulerPtr->lastQueueEntry[currentThread->thread->getParent()->getPriority()];

				this->currentThread->prev = lastEntry;
				this->currentThread->next = nullptr;

				if (this->currentThread->prev != nullptr) {
					this->currentThread->prev->next = this->currentThread;
				}

				schedulerPtr->lastQueueEntry[currentThread->thread->getParent()->getPriority()] = this->currentThread;
			} else {
				schedulerPtr->queues[currentThread->thread->getParent()->getPriority()] = this->currentThread;
				schedulerPtr->lastQueueEntry[currentThread->thread->getParent()->getPriority()] = this->currentThread;
			}
		}

		const ThreadListEntry *oldEntry = this->currentThread;

		if (schedulerPtr->readyThreadList != nullptr) {
			this->currentThread = schedulerPtr->readyThreadList;

			if (schedulerPtr->readyThreadList->next != nullptr) {
				schedulerPtr->readyThreadList->next->prev = nullptr;
			}

			schedulerPtr->readyThreadList = schedulerPtr->readyThreadList->next;

			// TODO: Make trampoline for user threads
		} else {
			ThreadListEntry *selectedEntry = nullptr;

			for (auto currQueue : schedulerPtr->queues) {
				while (currQueue != nullptr && currQueue->thread->getState() != ThreadState::RUNNING) {
					currQueue = currQueue->next;
				}

				if (currQueue != nullptr && currQueue->thread->getState() == ThreadState::RUNNING) {
					selectedEntry = currQueue;
				}
			}

			if (selectedEntry == nullptr) {
				CommonMain::getTerminal()->error("No thread to switch to for EN: %lu", "Scheduler", CpuManager::getCurrentCore()->cpuId); // TODO: Use custom panic

				Asm::lhlt();
			}

			schedulerPtr->queues[selectedEntry->thread->getParent()->getPriority()] = selectedEntry->next;
			selectedEntry->next->prev = nullptr;

			this->currentThread = selectedEntry;

			this->currentThread->next = nullptr;
			this->currentThread->prev = nullptr;
		}

		CommonMain::getTerminal()->debug("Switching from thread %lu to %lu", "Scheduler", oldEntry->thread->getId(), this->currentThread->thread->getId());

		switchContext(oldEntry->thread->getContext(), this->currentThread->thread->getContext());

		this->currentThread->thread->getParent()->getProcessContext()->pageMap.load();

		this->remainingTicks = maxTicks;
	}

	// Old Ctx = Current Thread, New Ctx = New Thread
	void ExecutionNode::switchContext(u64 *oldCtx, u64 *newCtx) {
		const auto *oldCtxConv = reinterpret_cast<ThreadContext *>(oldCtx);

		oldCtxConv->save();

		switchContextAsm(oldCtx, newCtx);

		oldCtxConv->load();
	}

	u64 *Scheduler::createContextArch(const bool isUser, const u64 rip, const u64 rsp) {
		auto *context = new ThreadContext(rsp, isUser);

		setStackAsm(reinterpret_cast<u64>(context->getStackPointer()), rip);

		return reinterpret_cast<u64 *>(context);
	}
}

namespace kernel::x86_64::threading {
	using namespace utils;

	ThreadContext::ThreadContext(const u64 stackPointer, const bool isUserspace) : isUser(isUserspace), stackPointer(stackPointer) {
		this->simdSave = reinterpret_cast<u64 *>(alignUp<u64>(reinterpret_cast<u64>(malloc(CpuId::getXSaveSize() + 64)), 64));

		CpuManager::initSimdContext(this->simdSave);
	}

	ThreadContext::~ThreadContext() {
		free(this->simdSave);
	}

	u64 *ThreadContext::getStackPointer() {
		return &this->stackPointer;
	}

	void ThreadContext::setStackPointer(const u64 stackPtr) {
		this->stackPointer = stackPtr;
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