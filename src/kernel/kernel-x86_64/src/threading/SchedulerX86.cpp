#include "SchedulerX86.hpp"

#include "CommonMain.hpp"
#include "memory/MainMemory.hpp"
#include "threading/Scheduler.hpp"

#include "hal/Cpu.hpp"
#include "hal/GDT.hpp"
#include "utils/Asm.hpp"
#include "utils/CpuId.hpp"

namespace kernel::common::threading {
	using namespace x86_64::threading;
	using namespace x86_64::utils;

	void ExecutionNode::schedule() {
		Asm::cli();

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

		if (this->currentThread) {
			if (this->currentThread->thread->getState() == ThreadState::RUNNING) {
				if (this->remainingTicks > 0) {
					this->remainingTicks--;

					return;
				}

				switchThreads();
			} else if (currentThread->thread->getState() == ThreadState::BLOCKED) {
				switchThreads();
			} else if (currentThread->thread->getState() == ThreadState::TERMINATED) {

			} else {

			}
		}

		Asm::sti();
	}

	void ExecutionNode::switchThreads() {
		Scheduler *schedulerPtr = CommonMain::getInstance()->getScheduler();

		// switchContext(currentThread->thread->getContext(), currentThread->thread->getContext());

		if (this->currentThread != nullptr) {
			ThreadListEntry *lastEntry = schedulerPtr->lastQueueEntry[currentThread->thread->getParent()->getPriority()];

			this->currentThread->prev = lastEntry;
			this->currentThread->next = nullptr;

			if (this->currentThread->prev != nullptr) {
				this->currentThread->prev->next = this->currentThread;
			}

			schedulerPtr->lastQueueEntry[currentThread->thread->getParent()->getPriority()] = this->currentThread;
		}

		ThreadListEntry *oldEntry = this->currentThread;

		/*ThreadListEntry *currEntry = schedulerPtr->queues[currentThread->thread->getParent()->getPriority()];

		this->currentThread = currEntry;

		if (this->currentThread != nullptr) {
			schedulerPtr->queues[currentThread->thread->getParent()->getPriority()] = this->currentThread->next;

			this->currentThread->next->prev = nullptr;
			this->currentThread->next = nullptr;
		}*/

		if (schedulerPtr->readyThreadList != nullptr) {
			this->currentThread = schedulerPtr->readyThreadList;

			schedulerPtr->readyThreadList->next->prev = nullptr;
			schedulerPtr->readyThreadList = schedulerPtr->readyThreadList->next;
		} else {
			for (auto currQueue : schedulerPtr->queues) {
				while (currQueue != nullptr) {

					currQueue = currQueue->next;
				}
			}
		}

		this->remainingTicks = maxTicks;
	}

	// Old Ctx = Current Thread, New Ctx = New Thread
	void ExecutionNode::switchContext(u64 *oldCtx, u64 *newCtx) {
		auto *oldCtxConv = reinterpret_cast<ThreadContext *>(oldCtx);
		auto *newCtxConv = reinterpret_cast<ThreadContext *>(newCtx);

		oldCtxConv->save();

		switchContextAsm(oldCtxConv->getStackPointer(), newCtxConv->getStackPointer());

		oldCtxConv->load();
	}

	u64 *Scheduler::createContextArch(const bool isUser, const u64 rip, const u64 rsp) {
		auto *context = new ThreadContext(rsp, isUser);

		u64 *currStackPointer = context->getStackPointer();

		// TODO: Maybe port this to asm
		currStackPointer -= 7;

		currStackPointer[6] = rip;

		return reinterpret_cast<u64 *>(context);
	}
}

namespace kernel::x86_64::threading {
	using namespace utils;

	ThreadContext::ThreadContext(const u64 stackPointer, const bool isUserspace) : isUser(isUserspace), stackPointer(stackPointer) {
		this->simdSave = static_cast<u64 *>(malloc(CpuId::getXSaveSize()));

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