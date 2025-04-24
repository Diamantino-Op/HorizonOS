#include "CommonMain.hpp"

extern limine_hhdm_request hhdmRequest;

namespace kernel::common {
	Terminal CommonMain::terminal;
	CommonMain *CommonMain::instance;

	void CommonMain::rootInit() {
		instance = this;
	}

	Terminal* CommonMain::getTerminal() {
		return &terminal;
	}

	CommonMain *CommonMain::getInstance() {
		return instance;
	}

	AllocContext *CommonMain::getKernelAllocContext() const {
		return this->kernelAllocContext;
	}

	u64 CommonMain::getCurrentHhdm() {
		if (hhdmRequest.response != nullptr) {
			return hhdmRequest.response->offset;
		}

		return 0x0;
	}

	uPtr CommonMain::getStackTop() const {
		return this->stackTop;
	}

	PhysicalMemoryManager *CommonMain::getPMM() {
		return &this->physicalMemoryManager;
	}

	VirtualMemoryManager *CommonMain::getVMM() {
		return &this->virtualMemoryManager;
	}

	void CommonMain::init() {}

	void CommonCoreMain::init() {}
}