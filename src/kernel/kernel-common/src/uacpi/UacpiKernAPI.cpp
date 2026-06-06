#include "UacpiKernAPI.hpp"

#include "CommonMain.hpp"
#include "memory/MainMemory.hpp"
#include "Math.hpp"
#include "utils/Asm.hpp"

#include "limine.h"

#include "uacpi/kernel_api.h"

extern limine_rsdp_request rsdpRequest;

using namespace kernel::common;
using namespace kernel::common::memory;
using namespace kernel::common::uacpi;

// Kernel API

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr *outRsdpAddress) {
	if (outRsdpAddress == nullptr) {
		return UACPI_STATUS_INTERNAL_ERROR;
	}

	if (rsdpRequest.response == nullptr) {
		*outRsdpAddress = 0;

		return UACPI_STATUS_INTERNAL_ERROR;
	}

	*outRsdpAddress = reinterpret_cast<uacpi_phys_addr>(rsdpRequest.response->address) - CommonMain::getCurrentHhdm();
	return UACPI_STATUS_OK;
}

void *uacpi_kernel_map(uacpi_phys_addr addr, uacpi_size len) {
	const u64 alignedAddr = alignDown<u64>(addr, pageSize);
	if (len == 0 || len > ~0ULL - addr) {
		return nullptr;
	}

	const u64 endAddr = addr + len;
	const u64 roundedLen = alignUp<u64>(endAddr, pageSize);
	const u64 hhdmBase = CommonMain::getCurrentHhdm();

	for (u64 i = alignedAddr; i < roundedLen; i += pageSize) {
		const u64 virtAddr = i + hhdmBase;

		CommonMain::getInstance()->getKernelAllocContext()->pageMap.mapPage(virtAddr, i, 0b00000011, false, false);
		kernel::x86_64::utils::Asm::invalidatePage(virtAddr);
	}

	return reinterpret_cast<u64 *>(addr + hhdmBase);
}

void uacpi_kernel_unmap(void *addr, uacpi_size len) {
	const u64 realAddr = reinterpret_cast<u64>(addr);
	const u64 alignedAddr = alignDown<u64>(realAddr, pageSize);
	const u64 offset = realAddr - alignedAddr;
	const u64 roundedLen = roundUp<u64>(len + offset, pageSize);

	for (u64 i = alignedAddr; i < alignedAddr + roundedLen; i += pageSize) {
		CommonMain::getInstance()->getKernelAllocContext()->pageMap.unMapPage(i, false);
	}
}

void uacpi_kernel_log(uacpi_log_level level, const uacpi_char* str) {
	Terminal* terminal = CommonMain::getTerminal();

	switch (level) {
		case UACPI_LOG_ERROR:
			terminal->printfUAcpi(false, "[    \o{33}[0;31merror    \o{33}[0m] \o{33}[1;30muACPI: \o{33}[0;37m%s\r\o{33}[0m", str);
			break;

		case UACPI_LOG_WARN:
			terminal->printfUAcpi(false, "[   \o{33}[0;33mwarning   \o{33}[0m] \o{33}[1;30muACPI: \o{33}[0;37m%s\r\o{33}[0m", str);
			break;

		case UACPI_LOG_INFO:
			terminal->printfUAcpi(false, "[ \o{33}[1;34minformation \o{33}[0m] \o{33}[1;30muACPI: \o{33}[0;37m%s\r\o{33}[0m", str);
			break;

		case UACPI_LOG_TRACE:
		case UACPI_LOG_DEBUG:
			terminal->printfUAcpi(false, "[    \o{33}[0;32mdebug    \o{33}[0m] \o{33}[1;30muACPI: \o{33}[0;37m%s\r\o{33}[0m", str);
			break;
	}
}

// API

#include "uacpi/uacpi.h"
#include "uacpi/event.h"
#include "uacpi/tables.h"
#include "uacpi/context.h"

namespace kernel::common::uacpi {
	void UAcpi::earlyInit() {
		uacpi_context_set_log_level(UACPI_LOG_INFO);

		// TODO: Maybe free this
		this->earlyInitTablePtr = CommonMain::getInstance()->getPMM()->allocPages(1, true);

		uacpi_setup_early_table_access(this->earlyInitTablePtr, pageSize);

		uacpi_table_fadt(&fadt);

		// Madt

		uacpi_table outTable;

		if (uacpi_table_find_by_signature(ACPI_MADT_SIGNATURE, &outTable) != UACPI_STATUS_OK) {
			return;
		}

		const auto *madtPtr = static_cast<acpi_madt *>(outTable.ptr);

		this->madt = static_cast<acpi_madt *>(malloc(madtPtr->hdr.length));

		memcpy(this->madt, madtPtr, madtPtr->hdr.length);

		uacpi_table_unref(&outTable);

		const auto madtStart = reinterpret_cast<uPtr>(this->madt->entries);
		const auto madtEnd = reinterpret_cast<uPtr>(this->madt) + this->madt->hdr.length;

		auto currMadt = reinterpret_cast<acpi_entry_hdr *>(madtStart);

		for (uPtr entry = madtStart; entry < madtEnd; entry += currMadt->length, currMadt = reinterpret_cast<acpi_entry_hdr *>(entry)) {
			switch (currMadt->type) {
				case 1:
					this->ioApicsAmount++;

					break;

				case 2:
					this->isosAmount++;

					break;

				default:
					break;
			}
		}

		this->ioApics = static_cast<acpi_madt_ioapic *>(malloc(this->ioApicsAmount * sizeof(acpi_madt_ioapic)));
		this->isos = static_cast<acpi_madt_interrupt_source_override *>(malloc(this->isosAmount * sizeof(acpi_madt_interrupt_source_override)));

		u64 i = 0;
		u64 j = 0;

		for (uPtr entry = madtStart; entry < madtEnd; entry += currMadt->length, currMadt = reinterpret_cast<acpi_entry_hdr *>(entry)) {
			switch (currMadt->type) {
				case 1:
					this->ioApics[i] = *reinterpret_cast<acpi_madt_ioapic *>(entry);

					i++;

					break;

				case 2:
					this->isos[j] = *reinterpret_cast<acpi_madt_interrupt_source_override *>(entry);

					j++;

					break;

				default:
					break;
			}
		}
	}

	auto UAcpi::getFadtTable() const -> acpi_fadt * {
		return this->fadt;
	}

	auto UAcpi::getMadtTable() const -> acpi_madt * {
		return this->madt;
	}

	auto UAcpi::getIoApics() const -> acpi_madt_ioapic * {
		return this->ioApics;
	}

	auto UAcpi::getIoApicsAmount() const -> u64 {
		return this->ioApicsAmount;
	}

	auto UAcpi::getIsos() const -> acpi_madt_interrupt_source_override * {
		return this->isos;
	}

	auto UAcpi::getIsosAmount() const -> u64 {
		return this->isosAmount;
	}
}