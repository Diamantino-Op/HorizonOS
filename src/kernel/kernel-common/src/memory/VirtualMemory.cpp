#include "VirtualMemory.hpp"

#include "CommonMain.hpp"
#include "MainMemory.hpp"
#include "Math.hpp"

#include "limine.h"

extern limine_executable_address_request kernelAddressRequest;
extern limine_memmap_request memMapRequest;

namespace kernel::common::memory {
	VirtualMemoryManager::VirtualMemoryManager(const u64 kernelStackTop) : kernelStackTop(kernelStackTop) {}

	void VirtualMemoryManager::init() {
		Terminal* terminal = CommonMain::getTerminal();

		PageMap *currentPageMap = &CommonMain::getInstance()->getKernelAllocContext()->pageMap;

		terminal->debug("Main page table allocated at: 0x%.16lx", "VMM", currentPageMap->getPageTable());
		terminal->debug("Kernel Stack Top Address: 0x%.16lx", "VMM", this->kernelStackTop);

		if (kernelAddressRequest.response != nullptr) {
			this->kernelAddrPhys = kernelAddressRequest.response->physical_base;
			this->kernelAddrVirt = kernelAddressRequest.response->virtual_base;
		}

		terminal->debug("Kernel Addresses:", "VMM");
		terminal->debug("	Virtual Addresses: 0x%.16lx", "VMM", this->kernelAddrVirt);
		terminal->debug("	Physical Addresses: 0x%.16lx", "VMM", this->kernelAddrPhys);

		terminal->debug("Sections:", "VMM");
		terminal->debug("	Limine: Start: 0x%.16lx, End: 0x%.16lx", "VMM", limineStart, limineEnd);
		terminal->debug("	Text: Start: 0x%.16lx, End: 0x%.16lx", "VMM", textStart, textEnd);
		terminal->debug("	RoData: Start: 0x%.16lx, End: 0x%.16lx", "VMM", rodataStart, rodataEnd);
		terminal->debug("	Data: Start: 0x%.16lx, End: 0x%.16lx", "VMM", dataStart, dataEnd);

		terminal->debug("Memmap entries:", "VMM");

		if (memMapRequest.response != nullptr) {
			for (u64 i = 0; i < memMapRequest.response->entry_count; i++) {
				const limine_memmap_entry *entry = memMapRequest.response->entries[i];

				const char *type;

				switch (entry->type) {
					case LIMINE_MEMMAP_USABLE:
						type = "Usable";
						break;

					case LIMINE_MEMMAP_RESERVED:
						type = "Reserved";
						break;

					case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
						type = "Acpi Reclaimable";
						break;

					case LIMINE_MEMMAP_ACPI_NVS:
						type = "Acpi Nvs";
						break;

					case LIMINE_MEMMAP_BAD_MEMORY:
						type = "Bad";
						break;

					case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
						type = "Bootloader Reclaimable";
						break;

					case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES:
						type = "Executable and Modules";
						break;

					case LIMINE_MEMMAP_FRAMEBUFFER:
						type = "Framebuffer";
						break;

					default:
						type = "Unknown";
						break;
				}

				terminal->debug("Entry %d:", "VMM", i);
				terminal->debug("	Start: 0x%.16lx, Size: %llu, Type: %s", "VMM", entry->base, entry->length, type);
			}
		}

		// Kernel Stack
		const u64 kernelStackTopAligned = alignUp<u64>(this->kernelStackTop, pageSize);

		for (u64 i = kernelStackTopAligned - (pageSize * 16); i < kernelStackTopAligned; i += pageSize) {
			currentPageMap->mapPage(i, i - CommonMain::getCurrentHhdm(), 0b00000011, true, true);
		}

		terminal->debug("Kernel Stack mapped!", "VMM");

		// Limine Section
		if (reinterpret_cast<u64>(&limineStart) - reinterpret_cast<u64>(&limineEnd) != 0) {
			const u64 limineStartAligned = alignDown<u64>(reinterpret_cast<u64>(&limineStart), pageSize);
			const u64 limineEndAligned = alignUp<u64>(reinterpret_cast<u64>(&limineEnd), pageSize);

			for (u64 i = limineStartAligned; i < limineEndAligned; i += pageSize) {
				currentPageMap->mapPage(i, i - this->kernelAddrVirt + this->kernelAddrPhys, 0b00000011, true, false);
			}

			terminal->debug("Limine Section mapped!", "VMM");
		} else {
			terminal->error("Limine Section size is 0!", "VMM");
		}

		// Text Section
		const u64 textStartAligned = alignDown<u64>(reinterpret_cast<u64>(&textStart), pageSize);
		const u64 textEndAligned = alignUp<u64>(reinterpret_cast<u64>(&textEnd), pageSize);

		for (u64 i = textStartAligned; i < textEndAligned; i += pageSize) {
			currentPageMap->mapPage(i, i - this->kernelAddrVirt + this->kernelAddrPhys, 0b00000001, true, false);
		}

		terminal->debug("Text Section mapped!", "VMM");

		// ROData Section
		const u64 rodataStartAligned = alignDown<u64>(reinterpret_cast<u64>(&rodataStart), pageSize);
		const u64 rodataEndAligned = alignUp<u64>(reinterpret_cast<u64>(&rodataEnd), pageSize);

		for (u64 i = rodataStartAligned; i < rodataEndAligned; i += pageSize) {
			currentPageMap->mapPage(i, i - this->kernelAddrVirt + this->kernelAddrPhys, 0b00000001, true, true);
		}

		terminal->debug("ROData Section mapped!", "VMM");

		// Data Section
		const u64 dataStartAligned = alignDown<u64>(reinterpret_cast<u64>(&dataStart), pageSize);
		const u64 dataEndAligned = alignUp<u64>(reinterpret_cast<u64>(&dataEnd), pageSize);

		for (u64 i = dataStartAligned; i < dataEndAligned; i += pageSize) {
			currentPageMap->mapPage(i, i - this->kernelAddrVirt + this->kernelAddrPhys, 0b00000011, true, true);
		}

		terminal->debug("Data Section mapped!", "VMM");

		// MemMap
		if (memMapRequest.response != nullptr) {
			for (u64 i = 0; i < memMapRequest.response->entry_count; i++) {
				const limine_memmap_entry *entry = memMapRequest.response->entries[i];

				const u64 entryStartAligned = alignDown<u64>(entry->base, pageSize);
				const u64 entryEndAligned = alignUp<u64>(entry->base + entry->length, pageSize);

				for (u64 j = entryStartAligned; j < entryEndAligned; j += pageSize) {
					currentPageMap->mapPage(j + CommonMain::getCurrentHhdm(), j, 0b00000011, true, false);
				}
			}
		}

		terminal->debug("Memory Sections mapped!", "VMM");

		currentPageMap->load();
	}

	u64 VirtualMemoryManager::getVirtualKernelAddr() const {
		return this->kernelAddrVirt;
	}

	bool PageMap::level5Paging() const {
		return this->isLevel5Paging;
	}

	u64 *PageMap::getPageTable() const {
		return this->pageTable;
	}
}