#include "CommonMain.hpp"

#include "threading/IDAllocator.hpp"

extern limine_hhdm_request hhdmRequest;

namespace kernel::common {
	Terminal CommonMain::terminal;
	CommonMain *CommonMain::instance;

	void CommonMain::rootInit() {
		instance = this;

		PIDAllocator::init();
		TIDAllocator::init();
	}

	bool CommonMain::isInit() const {
		return this->isInitFlag;
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

	VirtualPageAllocator *CommonMain::getVPA() {
		return &this->virtualPageAllocator;
	}

	Clocks *CommonMain::getClocks() {
		return &this->clocks;
	}

	UAcpi *CommonMain::getUAcpi() {
		return &this->uAcpi;
	}

	AcpiPM *CommonMain::getAcpiPM() {
		return &this->acpiPM;
	}

	Scheduler *CommonMain::getScheduler() const {
		return this->scheduler;
	}

	void CommonMain::init() {}

	void CommonMain::shutdown() {}

	void CommonCoreMain::init() {}
}