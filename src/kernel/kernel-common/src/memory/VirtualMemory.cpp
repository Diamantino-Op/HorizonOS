#include "VirtualMemory.hpp"

#include "CommonMain.hpp"
#include "MainMemory.hpp"

namespace kernel::common::memory {
	template <class T> void VirtualMemoryManager<T>::init() {
		Terminal* terminal = CommonMain::getTerminal();

		terminal->printf("Main page table allocated at: %lp\n", reinterpret_cast<uPtr *>(&this->currentMainPage));

		memset(this->currentMainPage, 0, pageSize);

		terminal->printf("Kernel Stack Top Address: %lp\n", CommonMain::getStackTop());

		if (kernelAddressRequest.response != nullptr) {
			this->kernelAddrPhys = kernelAddressRequest.response->physical_base;
			this->kernelAddrVirt = kernelAddressRequest.response->virtual_base;
		}

		terminal->printf("Kernel Addresses:\n");
		terminal->printf("	Virtual Addresses: %lp\n", this->kernelAddrVirt);
		terminal->printf("	Physical Addresses: %lp\n", this->kernelAddrPhys);

		terminal->printf("Sections:\n");
		terminal->printf("	Limine: Start: %lp, End: %lp\n", limineStart, limineEnd);
		terminal->printf("	Text: Start: %lp, End: %lp\n", textStart, textEnd);
		terminal->printf("	RoData: Start: %lp, End: %lp\n", rodataStart, rodataEnd);
		terminal->printf("	Data: Start: %lp, End: %lp\n", dataStart, dataEnd);
	}

	template<class T> void VirtualMemoryManager<T>::handlePageFault(u64 faultAddr, u8 flags) {
		u64 alignedAddr = faultAddr & ~(0x1000 - 1);
		// u64 physAddress = allocatePage();

		this->mapPage(alignedAddr, (u64) nullptr, flags);
	}
}