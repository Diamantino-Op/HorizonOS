#include "SchedulerX86.hpp"

#include "memory/MainMemory.hpp"
#include "threading/Scheduler.hpp"

#include "hal/Cpu.hpp"
#include "hal/GDT.hpp"
#include "utils/CpuId.hpp"

namespace kernel::common::threading {
	using namespace x86_64::threading;

	void ExecutionNode::schedule() {
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
	}

	void ExecutionNode::switchContext(u64 *oldCtx, u64 *newCtx) {
		// Old Ctx
		auto *oldCtxConv = reinterpret_cast<ThreadContext *>(oldCtx);
		Frame *oldFrame = oldCtxConv->getFrame();

		oldCtxConv->save();

		asm volatile("lea (%%rip), %0" : "=r"(oldFrame->rip));

		asm volatile("mov %%rax, %0" : "=r"(oldFrame->rax));
		asm volatile("mov %%rbx, %0" : "=r"(oldFrame->rbx));
		asm volatile("mov %%rcx, %0" : "=r"(oldFrame->rcx));
		asm volatile("mov %%rdx, %0" : "=r"(oldFrame->rdx));
		asm volatile("mov %%rsi, %0" : "=r"(oldFrame->rsi));
		asm volatile("mov %%rdi, %0" : "=r"(oldFrame->rdi));
		asm volatile("mov %%rbp, %0" : "=r"(oldFrame->rbp));
		asm volatile("mov %%r8, %0" : "=r"(oldFrame->r8));
		asm volatile("mov %%r9, %0" : "=r"(oldFrame->r9));
		asm volatile("mov %%r10, %0" : "=r"(oldFrame->r10));
		asm volatile("mov %%r11, %0" : "=r"(oldFrame->r11));
		asm volatile("mov %%r12, %0" : "=r"(oldFrame->r12));
		asm volatile("mov %%r13, %0" : "=r"(oldFrame->r13));
		asm volatile("mov %%r14, %0" : "=r"(oldFrame->r14));
		asm volatile("mov %%r15, %0" : "=r"(oldFrame->r15));

		asm volatile("mov %%ss, %0" : "=r"(oldFrame->ss));
		asm volatile("mov %%cs, %0" : "=r"(oldFrame->cs));

		asm volatile("pushf");
		asm volatile("pop %0" : "=r"(oldFrame->rFlags));

		asm volatile("mov %%rsp, %0" : "=r"(oldFrame->rsp));

		// New Ctx
		auto *newCtxConv = reinterpret_cast<ThreadContext *>(newCtx);
		Frame *newFrame = newCtxConv->getFrame();

		asm volatile("mov %0, %%rsp" :: "a"(newFrame->rsp));

		asm volatile("mov %0, %%rax" :: "a"(newFrame->rax));
		asm volatile("mov %0, %%rbx" :: "a"(newFrame->rbx));
		asm volatile("mov %0, %%rcx" :: "a"(newFrame->rcx));
		asm volatile("mov %0, %%rdx" :: "a"(newFrame->rdx));
		asm volatile("mov %0, %%rsi" :: "a"(newFrame->rsi));
		asm volatile("mov %0, %%rdi" :: "a"(newFrame->rdi));
		asm volatile("mov %0, %%rbp" :: "a"(newFrame->rbp));
		asm volatile("mov %0, %%r8" :: "a"(newFrame->r8));
		asm volatile("mov %0, %%r9" :: "a"(newFrame->r9));
		asm volatile("mov %0, %%r10" :: "a"(newFrame->r10));
		asm volatile("mov %0, %%r11" :: "a"(newFrame->r11));
		asm volatile("mov %0, %%r12" :: "a"(newFrame->r12));
		asm volatile("mov %0, %%r13" :: "a"(newFrame->r13));
		asm volatile("mov %0, %%r14" :: "a"(newFrame->r14));
		asm volatile("mov %0, %%r15" :: "a"(newFrame->r15));

		asm volatile("mov %0, %%ss" :: "a"(newFrame->ss));
		asm volatile("mov %0, %%cs" :: "a"(newFrame->cs));

		asm volatile("push %0" :: "a"(newFrame->rFlags));
		asm volatile("popf");

		newCtxConv->load();

		asm volatile("jmp *%0" :: "r"(newFrame->rip));
	}

	u64 *Scheduler::createContext(const bool isUser, const u64 rip, const u64 rsp) {
		auto *context = new ThreadContext();

		context->getFrame()->rip = rip;
		context->getFrame()->rsp = rsp;
		context->getFrame()->rFlags = 0x202;

		if (isUser) {
			context->getFrame()->cs = Selector::USER_CODE | 3;
			context->getFrame()->ss = Selector::USER_DATA | 3;
		} else {
			context->getFrame()->cs = Selector::KERNEL_CODE;
			context->getFrame()->ss = Selector::KERNEL_DATA;
		}

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

	Frame *ThreadContext::getFrame() {
		return &this->frame;
	}

	void ThreadContext::save() const {
		CpuManager::saveSimdContext(this->simdSave);
	}

	void ThreadContext::load() const {
		CpuManager::loadSimdContext(this->simdSave);
	}
}