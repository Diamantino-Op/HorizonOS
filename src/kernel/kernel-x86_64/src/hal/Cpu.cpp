#define LIMINE_API_REVISION 3

#include "Cpu.hpp"

#include "CommonMain.hpp"
#include "utils/CpuId.hpp"

#include "limine.h"

extern limine_mp_request mpRequest;

namespace kernel::x86_64::hal {
	using namespace common;
	using namespace utils;

	void CpuManager::init() {
		if (mpRequest.response != nullptr) {
			Terminal* terminal = CommonMain::getTerminal();

			this->brand = CpuId::getBrand();
			this->vendor = CpuId::getVendor();

			this->coreAmount = mpRequest.response->cpu_count;

			terminal->info("Brand: %s", "Cpu", this->brand);
			terminal->info("Vendor: %s", "Cpu", this->vendor);

			terminal->info("Cores: %u", "Cpu", this->coreAmount);

			this->initSimd();
		}
	}

	void CpuManager::initSimd() {

	}
}