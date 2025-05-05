#include "SchedulerX86.hpp"

#include "memory/MainMemory.hpp"
#include "threading/Scheduler.hpp"

#include "hal/Cpu.hpp"
#include "hal/GDT.hpp"
#include "utils/CpuId.hpp"

namespace kernel::common::threading {
	using namespace x86_64::threading;

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

	void ThreadContext::save(const Frame &frame) {
		CpuManager::saveSimdContext(this->simdSave);

		this->frame = frame;
	}

	void ThreadContext::load(Frame &frame) const {
		frame = this->frame;

		CpuManager::loadSimdContext(this->simdSave);
	}
}