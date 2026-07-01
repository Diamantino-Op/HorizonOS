#include "CommonMain.hpp"

#include "threading/IDAllocator.hpp"

extern limine_hhdm_request hhdmRequest;
extern limine_rsdp_request rsdpRequest;
extern limine_mp_request mpRequest;

namespace kernel::common {
	Terminal CommonMain::terminal;
	CommonMain *CommonMain::instance;
	u64 CommonMain::currentHhdm;
	u64 CommonMain::currentRsdp;
	u64 CommonMain::schedulingCoresRemaining;

	void CommonMain::rootInit() {
		instance = this;

		if (hhdmRequest.response != nullptr) {
			currentHhdm = hhdmRequest.response->offset;
		} else {
			currentHhdm = 0x0;
		}

		if (rsdpRequest.response != nullptr) {
			currentRsdp = reinterpret_cast<u64>(rsdpRequest.response->address);
		} else {
			currentRsdp = 0x0;
		}

		if (mpRequest.response != nullptr) {
			schedulingCoresRemaining = mpRequest.response->cpu_count;
		}

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

	AllocContext *CommonMain::getKernelAllocContextHHDM() const {
		return this->kernelAllocContextHHDM;
	}

	u64 CommonMain::getCurrentHhdm() {
		return currentHhdm;
	}

	u64 CommonMain::getCurrentRsdp() {
		return currentRsdp;
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