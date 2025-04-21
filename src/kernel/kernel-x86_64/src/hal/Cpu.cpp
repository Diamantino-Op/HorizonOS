#define LIMINE_API_REVISION 3

#include "Cpu.hpp"

#include "limine.h"

extern limine_mp_request mpRequest;

namespace kernel::x86_64::hal {
	void CpuManager::init() {
		this->initSimd();
	}

	void CpuManager::initSimd() {

	}
}