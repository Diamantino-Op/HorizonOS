#include "VirtualMemory.hpp"

#include "CommonMain.hpp"
#include "MainMemory.hpp"
#include "Math.hpp"

#include "limine.h"

__attribute__((used, section(".limine_requests")))
static volatile limine_executable_address_request kernelAddressRequest = {
	.id = LIMINE_EXECUTABLE_ADDRESS_REQUEST,
	.revision = 0,
	.response = nullptr,
};

__attribute__((used, section(".limine_requests")))
static volatile limine_memmap_request memMapRequest = {
	.id = LIMINE_MEMMAP_REQUEST,
	.revision = 0,
	.response = nullptr,
};

namespace kernel::common::memory {
	VirtualMemoryManager::VirtualMemoryManager(u64 kernelStackTop) : kernelStackTop(kernelStackTop) {}

	void VirtualMemoryManager::init() {
		Terminal* terminal = CommonMain::getTerminal();

		terminal->printf("Main page table allocated at: %lp\n", reinterpret_cast<uPtr *>(&this->currentMainPage));

		// memset(this->currentMainPage, 0, pageSize); TODO: Alloc first

		terminal->printf("Kernel Stack Top Address: %lp\n", this->kernelStackTop);

		if (kernelAddressRequest.response != nullptr) {
			this->kernelAddrPhys = kernelAddressRequest.response->physical_base;
			this->kernelAddrVirt = kernelAddressRequest.response->virtual_base;
		}

		terminal->printf("Kernel Addresses:\n");
		terminal->printf("	Virtual Addresses: 0x%.16lx\n", this->kernelAddrVirt);
		terminal->printf("	Physical Addresses: 0x%.16lx\n", this->kernelAddrPhys);

		terminal->printf("Sections:\n");
		terminal->printf("	Limine: Start: 0x%.16lx, End: 0x%.16lx\n", limineStart, limineEnd);
		terminal->printf("	Text: Start: 0x%.16lx, End: 0x%.16lx\n", textStart, textEnd);
		terminal->printf("	RoData: Start: 0x%.16lx, End: 0x%.16lx\n", rodataStart, rodataEnd);
		terminal->printf("	Data: Start: 0x%.16lx, End: 0x%.16lx\n", dataStart, dataEnd);

		if (memMapRequest.response != nullptr) {
			u64 total = 0;
			u64 usable = 0;
			u64 usableAmount = 0;

			for (u64 i = 0; i < memMapRequest.response->entry_count; i++) {
				limine_memmap_entry *entry = memMapRequest.response->entries[i];

				if (entry->type == LIMINE_MEMMAP_USABLE) {
					usableAmount++;
				}
			}

			UsableMemory tempUsableMemory[usableAmount] = {};

			this->usableMemory = tempUsableMemory;

			usableAmount = 0;

			for (u64 i = 0; i < memMapRequest.response->entry_count; i++) {
				limine_memmap_entry *entry = memMapRequest.response->entries[i];

				total += entry->length;

				if (entry->type == LIMINE_MEMMAP_USABLE) {
					usable += entry->length;

					this->usableMemory[usableAmount] = {
						entry->base,
						entry->length,
					};

					usableAmount++;
				}

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

				terminal->printf("Entry %d, Start: 0x%.16lx, Size: %llu, %s\n", i, entry->base, entry->length, type);
			}

			terminal->printf("Total Memory: %llu\n", total);
			terminal->printf("Usable Memory: %llu\n", usable);
		}

		// Kernel Stack
		u64 kernelStackTopAligned = alignUp<u64>(this->kernelStackTop, pageSize);

		for (u64 i = kernelStackTopAligned - (pageSize * 16); i < kernelStackTopAligned; i += pageSize) {
			this->mapPage(i, i - this->currentHhdm, 0b00000011, true);
		}

		terminal->printf("Kernel Stack mapped!\n");

		// Limine Section
		if (reinterpret_cast<u64>(&limineStart) - reinterpret_cast<u64>(&limineEnd) != 0) {
			u64 limineStartAligned = alignDown<u64>(reinterpret_cast<u64>(&limineStart), pageSize);
			u64 limineEndAligned = alignUp<u64>(reinterpret_cast<u64>(&limineEnd), pageSize);

			for (u64 i = limineStartAligned; i < limineEndAligned; i += pageSize) {
				this->mapPage(i, i - this->kernelAddrVirt + this->kernelAddrPhys, 0b00000011, false);
			}

			terminal->printf("Limine Section mapped!\n");
		} else {
			terminal->printf("\033[0;31mLimine Section size is 0!\n");
		}

		// Text Section
		u64 textStartAligned = alignDown<u64>(reinterpret_cast<u64>(&textStart), pageSize);
		u64 textEndAligned = alignUp<u64>(reinterpret_cast<u64>(&textEnd), pageSize);

		for (u64 i = textStartAligned; i < textEndAligned; i += pageSize) {
			this->mapPage(i, i - this->kernelAddrVirt + this->kernelAddrPhys, 0b00000001, false);
		}

		terminal->printf("Text Section mapped!\n");

		// ROData Section
		u64 rodataStartAligned = alignDown<u64>(reinterpret_cast<u64>(&rodataStart), pageSize);
		u64 rodataEndAligned = alignUp<u64>(reinterpret_cast<u64>(&rodataEnd), pageSize);

		for (u64 i = rodataStartAligned; i < rodataEndAligned; i += pageSize) {
			this->mapPage(i, i - this->kernelAddrVirt + this->kernelAddrPhys, 0b00000001, true);
		}

		terminal->printf("ROData Section mapped!\n");

		// Data Section
		u64 dataStartAligned = alignDown<u64>(reinterpret_cast<u64>(&dataStart), pageSize);
		u64 dataEndAligned = alignUp<u64>(reinterpret_cast<u64>(&dataEnd), pageSize);

		for (u64 i = dataStartAligned; i < dataEndAligned; i += pageSize) {
			this->mapPage(i, i - this->kernelAddrVirt + this->kernelAddrPhys, 0b00000011, true);
		}

		terminal->printf("Data Section mapped!\n");

		// MemMap
		if (memMapRequest.response != nullptr) {
			for (u64 i = 0; i < memMapRequest.response->entry_count; i++) {
				limine_memmap_entry *entry = memMapRequest.response->entries[i];

				u64 entryStartAligned = alignDown<u64>(entry->base, pageSize);
				u64 entryEndAligned = alignUp<u64>(entry->base + entry->length, pageSize);

				for (u64 j = entryStartAligned; j < entryEndAligned; j += pageSize) {
					this->mapPage(j + this->currentHhdm, j, 0b00000011, false);
				}
			}
		}

		terminal->printf("Memory Sections mapped!\n");

		this->loadPageTable();
	}

	void VirtualMemoryManager::handlePageFault(u64 faultAddr, u8 flags) {
		u64 alignedAddr = faultAddr & ~(0x1000 - 1);
		// u64 physAddress = allocatePage();

		this->mapPage(alignedAddr, (u64) nullptr, flags, false);
	}
}