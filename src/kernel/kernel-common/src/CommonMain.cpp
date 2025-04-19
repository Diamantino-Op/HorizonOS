#include "CommonMain.hpp"

using namespace kernel::common;

Terminal CommonMain::terminal;

namespace kernel::common {
	CommonMain::CommonMain() {
		instance = this;
	}

	Terminal* CommonMain::getTerminal() {
		return &terminal;
	}

	CommonMain *CommonMain::getInstance() {
		return instance;
	}

	AllocContext *CommonMain::getKernelAllocContext() {
		return &this->kernelAllocContext;
	}
}