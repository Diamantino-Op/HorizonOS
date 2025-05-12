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

		if (CommonMain::getInstance()->getScheduler()->readyThreadList) {

		}

		if (currentThread) {
			if (currentThread->thread->getState() == ThreadState::RUNNING) {
				if (this->remainingTicks > 0) {
					this->remainingTicks--;

					return;
				}
			}

			if (currentThread->thread->getState() == ThreadState::TERMINATED) {
				delete currentThread->thread;

				delete currentThread;
			} else {

			}
		}

		Asm::sti();
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
		auto *context = new ThreadContext();

		context->setStackPointer(rsp);

		u64 *currStackPointer = context->getStackPointer();

		// TODO: Maybe port this to asm
		currStackPointer -= 7;

		currStackPointer[6] = rip;

		return reinterpret_cast<u64 *>(context);
	}
}

namespace kernel::x86_64::threading {
	using namespace utils;

	ThreadContext::ThreadContext() {
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
}