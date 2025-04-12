#include "VirtualMemory.hpp"

#include "CommonMain.hpp"
#include "MainMemory.hpp"

__attribute__((used, section(".limine_requests")))
static volatile limine_kernel_address_request kernelAddressRequest = {
	.id = LIMINE_KERNEL_ADDRESS_REQUEST,
	.revision = 0,
	.response = nullptr,
};

namespace kernel::common::memory {
	void VirtualMemoryManager::init() {
		Terminal* terminal = CommonMain::getTerminal();

		terminal->printf("Main page table allocated at: %lp\n", reinterpret_cast<uPtr *>(&this->currentMainPage));

		// memset(this->currentMainPage, 0, pageSize); TODO: Alloc first

		terminal->printf("Kernel Stack Top Address: %lp\n", CommonMain::getStackTop());

		if (kernelAddressRequest.response != nullptr) {
			this->kernelAddrPhys = kernelAddressRequest.response->physical_base;
			this->kernelAddrVirt = kernelAddressRequest.response->virtual_base;
		}

		terminal->printf("Kernel Addresses:\n");
		terminal->printf("	Virtual Addresses: %lp\n", this->kernelAddrVirt);
		terminal->printf("	Physical Addresses: %lp\n", this->kernelAddrPhys);

		terminal->printf("Sections:\n");
		terminal->printf("	Limine: Start: 0x%.16llx, End: 0x%.16llx\n", &limineStart, &limineEnd);
		terminal->printf("	Text: Start: 0x%.16llx, End: 0x%.16llx\n", &textStart, &textEnd);
		terminal->printf("	RoData: Start: 0x%.16llx, End: 0x%.16llx\n", &rodataStart, &rodataEnd);
		terminal->printf("	Data: Start: 0x%.16llx, End: 0x%.16llx\n", &dataStart, &dataEnd);
	}

	void VirtualMemoryManager::handlePageFault(u64 faultAddr, u8 flags) {
		u64 alignedAddr = faultAddr & ~(0x1000 - 1);
		// u64 physAddress = allocatePage();

		this->mapPage(alignedAddr, (u64) nullptr, flags);
	}
}