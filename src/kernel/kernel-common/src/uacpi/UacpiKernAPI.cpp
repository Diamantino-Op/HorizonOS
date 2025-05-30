#define UACPI_NATIVE_ALLOC_ZEROED

#include "UacpiKernAPI.hpp"

#include "CommonMain.hpp"
#include "memory/MainMemory.hpp"
#include "Math.hpp"
#include "Event.hpp"

#include "limine.h"

#include "uacpi/kernel_api.h"

extern limine_rsdp_request rsdpRequest;

using namespace kernel::common;
using namespace kernel::common::memory;
using namespace kernel::common::uacpi;

// Kernel API

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr *outRsdpAddress) {
	if (rsdpRequest.response != nullptr) {
		*outRsdpAddress = rsdpRequest.response->address;
	}

	return UACPI_STATUS_OK;
}

void *uacpi_kernel_map(uacpi_phys_addr addr, uacpi_size len) {
	const u64 alignedAddr = alignDown<u64>(addr, pageSize);
	const u64 offset = addr - alignedAddr;
	const u64 roundedLen = roundUp<u64>(len + offset, pageSize);

	for (u64 i = alignedAddr; i < alignedAddr + roundedLen; i += pageSize) {
		CommonMain::getInstance()->getKernelAllocContext()->pageMap.mapPage(alignedAddr + CommonMain::getCurrentHhdm(), alignedAddr, 0b00000011, false, false);
	}

	return reinterpret_cast<u64 *>(addr + CommonMain::getCurrentHhdm());
}

void uacpi_kernel_unmap(void *addr, uacpi_size len) {
	const u64 realAddr = reinterpret_cast<u64>(addr);
	const u64 alignedAddr = alignDown<u64>(realAddr, pageSize);
	const u64 offset = realAddr - alignedAddr;
	const u64 roundedLen = roundUp<u64>(len + offset, pageSize);

	for (u64 i = alignedAddr; i < alignedAddr + roundedLen; i += pageSize) {
		CommonMain::getInstance()->getKernelAllocContext()->pageMap.unMapPage(alignedAddr);
	}
}

u64 *lastAllocatedAddr = nullptr;

void *uacpi_kernel_alloc(uacpi_size size) {
	void *mem = malloc(size);

	if (mem == nullptr) {
		CommonMain::getTerminal()->debug("Allocating %u bytes, failed: 0x%.16lx, last address: 0x%.16lx", "uACPI", size, mem, lastAllocatedAddr);
	} else {
		lastAllocatedAddr = static_cast<u64 *>(mem);
	}

	return mem;
}

void *uacpi_kernel_alloc_zeroed(uacpi_size size) {
	void *mem = malloc(size);

	if (mem == nullptr) {
		CommonMain::getTerminal()->debug("Allocating zeroed %u bytes, failed: 0x%.16lx, last address: 0x%.16lx", "uACPI", size, mem, lastAllocatedAddr);
	} else {
		lastAllocatedAddr = static_cast<u64 *>(mem);
	}

	return mem;
}

void uacpi_kernel_free(void *mem) {
	free(mem);
}

void uacpi_kernel_log(uacpi_log_level level, const uacpi_char* str) {
	Terminal* terminal = CommonMain::getTerminal();

	switch (level) {
		case UACPI_LOG_ERROR:
			terminal->printfLock(false, "[    \033[0;31merror    \033[0m] \033[1;30muACPI: \033[0;37m%s\033[0m", str);
			break;

		case UACPI_LOG_WARN:
			terminal->printfLock(false, "[   \033[0;33mwarning   \033[0m] \033[1;30muACPI: \033[0;37m%s\033[0m", str);
			break;

		case UACPI_LOG_INFO:
			terminal->printfLock(false, "[ \033[1;34minformation \033[0m] \033[1;30muACPI: \033[0;37m%s\033[0m", str);
			break;

		case UACPI_LOG_TRACE:
		case UACPI_LOG_DEBUG:
			terminal->printfLock(false, "[    \033[0;32mdebug    \033[0m] \033[1;30muACPI: \033[0;37m%s\033[0m", str);
			break;
	}
}

uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot() {
	return CommonMain::getInstance()->getClocks()->getMainClock()->getNs();
}

void uacpi_kernel_stall(uacpi_u8 uSec) {
	CommonMain::getInstance()->getClocks()->stallNs(uSec * 1000);
}

// PCI

uacpi_status uacpi_kernel_pci_device_open(uacpi_pci_address address, uacpi_handle *out_handle) {
	return UACPI_STATUS_UNIMPLEMENTED;
}

void uacpi_kernel_pci_device_close(uacpi_handle handle) {

}

uacpi_status uacpi_kernel_pci_read8(uacpi_handle device, uacpi_size offset, uacpi_u8 *value) {
	return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_pci_read16(uacpi_handle device, uacpi_size offset, uacpi_u16 *value) {
	return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_pci_read32(uacpi_handle device, uacpi_size offset, uacpi_u32 *value) {
	return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_pci_write8(uacpi_handle device, uacpi_size offset, uacpi_u8 value) {
	return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_pci_write16(uacpi_handle device, uacpi_size offset, uacpi_u16 value) {
	return UACPI_STATUS_UNIMPLEMENTED;
}

uacpi_status uacpi_kernel_pci_write32(uacpi_handle device, uacpi_size offset, uacpi_u32 value) {
	return UACPI_STATUS_UNIMPLEMENTED;
}

// I/O

// TODO: Use new VPage map

uacpi_status uacpi_kernel_io_map(uacpi_io_addr base, uacpi_size len, uacpi_handle *out_handle) {
	*out_handle = reinterpret_cast<u64 *>(base); // TODO: to fix (for other arches)

	return UACPI_STATUS_OK;
}

void uacpi_kernel_io_unmap(uacpi_handle handle) {

}

uacpi_status uacpi_kernel_io_read8(uacpi_handle handle, uacpi_size offset, uacpi_u8 *out_value) {
	return uacpiKernelIoRead8(handle, offset, out_value);
}

uacpi_status uacpi_kernel_io_read16(uacpi_handle handle, uacpi_size offset, uacpi_u16 *out_value) {
	return uacpiKernelIoRead16(handle, offset, out_value);
}

uacpi_status uacpi_kernel_io_read32(uacpi_handle handle, uacpi_size offset, uacpi_u32 *out_value) {
	return uacpiKernelIoRead32(handle, offset, out_value);
}

uacpi_status uacpi_kernel_io_write8(uacpi_handle handle, uacpi_size offset, uacpi_u8 in_value) {
	return uacpiKernelIoWrite8(handle, offset, in_value);
}

uacpi_status uacpi_kernel_io_write16(uacpi_handle handle, uacpi_size offset, uacpi_u16 in_value) {
	return uacpiKernelIoWrite16(handle, offset, in_value);
}

uacpi_status uacpi_kernel_io_write32(uacpi_handle handle, uacpi_size offset, uacpi_u32 in_value) {
	return uacpiKernelIoWrite32(handle, offset, in_value);
}

// Threads
// TODO: Create threads first
void uacpi_kernel_sleep(uacpi_u64 mSec) {
	CommonMain::getInstance()->getClocks()->stallNs(mSec * 1'000'000ull); // TODO: use real sleep
}

uacpi_thread_id uacpi_kernel_get_thread_id() {
	return reinterpret_cast<void *>(1);
}

uacpi_handle uacpi_kernel_create_mutex() {
	return new SimpleSpinLock();
}

void uacpi_kernel_free_mutex(uacpi_handle handle) {
	free(handle);
}

uacpi_status uacpi_kernel_acquire_mutex(uacpi_handle handle, uacpi_u16 timeout) {
	static_cast<SimpleSpinLock *>(handle)->lock();

	return UACPI_STATUS_OK;
}

void uacpi_kernel_release_mutex(uacpi_handle handle) {
	static_cast<SimpleSpinLock *>(handle)->unlock();
}

uacpi_bool uacpi_kernel_wait_for_event(uacpi_handle handle, uacpi_u16 timeout) {
	const auto event = static_cast<SimpleEvent *>(handle);

	if (timeout == 0xFFFF) {
		while (!event->decrement()) {
			uacpi_kernel_sleep(10);
		}

		return true;
	}

	i64 remaining = timeout;

	while (!event->decrement()) {
		if (remaining <= 0) {
			return false;
		}

		uacpi_kernel_sleep(10);
		remaining -= 10;
	}

	return true;
}

uacpi_handle uacpi_kernel_create_event() {
	return new SimpleEvent(0);
}

void uacpi_kernel_free_event(uacpi_handle handle) {
	delete static_cast<SimpleEvent *>(handle);
}

void uacpi_kernel_signal_event(uacpi_handle handle) {
	static_cast<SimpleEvent *>(handle)->add(1);
}

void uacpi_kernel_reset_event(uacpi_handle handle) {
	static_cast<SimpleEvent *>(handle)->set(0);
}

uacpi_status uacpi_kernel_schedule_work(uacpi_work_type workType, uacpi_work_handler workHandler, uacpi_handle ctx) {
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_wait_for_work_completion() {
	return UACPI_STATUS_OK;
}

// Interrupts

uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request *request) {
	return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_install_interrupt_handler(uacpi_u32 irq, uacpi_interrupt_handler intHandler, uacpi_handle ctx, uacpi_handle *out_irq_handle) {
	return uacpiKernelInstallInterruptHandler(irq, intHandler, ctx, out_irq_handle);
}

uacpi_status uacpi_kernel_uninstall_interrupt_handler(uacpi_interrupt_handler intHandler, uacpi_handle irq_handle) {
	return uacpiKernelUninstallInterruptHandler(intHandler, irq_handle);
}

uacpi_handle uacpi_kernel_create_spinlock() {
	return new TicketSpinLock();
}

void uacpi_kernel_free_spinlock(uacpi_handle handle) {
	delete static_cast<TicketSpinLock *>(handle);
}

uacpi_cpu_flags uacpi_kernel_lock_spinlock(uacpi_handle handle) {
	static_cast<TicketSpinLock *>(handle)->lock();

	return 0;
}

void uacpi_kernel_unlock_spinlock(uacpi_handle handle, uacpi_cpu_flags) {
	static_cast<TicketSpinLock *>(handle)->unlock();
}

// API

#include "uacpi/uacpi.h"
#include "uacpi/event.h"
#include "uacpi/sleep.h"
#include "uacpi/tables.h"
#include "uacpi/context.h"

namespace kernel::common::uacpi {
	void UAcpi::init() {
		Terminal* terminal = CommonMain::getTerminal();

		if (const uacpi_status ret = uacpi_initialize(0); uacpi_unlikely_error(ret)) {
			terminal->error("Failed to initialize: %s", "uAcpi", uacpi_status_to_string(ret));
		}

		if (const uacpi_status ret = uacpi_namespace_load(); uacpi_unlikely_error(ret)) {
			terminal->error("Failed to load namespaces: %s", "uAcpi", uacpi_status_to_string(ret));
		}

		this->archMiddleInit();

		if (const uacpi_status ret = uacpi_namespace_initialize(); uacpi_unlikely_error(ret)) {
			terminal->error("Failed to initialize namespaces: %s", "uAcpi", uacpi_status_to_string(ret));
		}

		if (const uacpi_status ret = uacpi_finalize_gpe_initialization(); uacpi_unlikely_error(ret)) {
			terminal->error("Failed to initialize GPEs: %s", "uAcpi", uacpi_status_to_string(ret));
		}

		// Free early init table

		CommonMain::getInstance()->getPMM()->freePages(this->earlyInitTablePtr, 1);

		// Events

		if (const uacpi_status ret = uacpi_install_fixed_event_handler(UACPI_FIXED_EVENT_POWER_BUTTON, &handlerPowerBtn, nullptr); uacpi_unlikely_error(ret)) {
			terminal->error("Failed to install pwr button handler: %s", "uAcpi", uacpi_status_to_string(ret));
		}
	}

	void UAcpi::earlyInit() {
		uacpi_context_set_log_level(UACPI_LOG_INFO);

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
					// TODO: Add to IOApic table (reinterpret_cast<acpi_madt_ioapic *>(entry))
					break;

				case 2:
					// TODO: Add to interrupt src override table (reinterpret_cast<acpi_madt_interrupt_source_override *>(entry))
					break;

				default:
					break;
			}
		}
	}

	void UAcpi::shutdown() {
		Terminal* terminal = CommonMain::getTerminal();

		if (const uacpi_status ret = uacpi_prepare_for_sleep_state(UACPI_SLEEP_STATE_S5); uacpi_unlikely_error(ret)) {
			terminal->error("Failed to prepare for S5: %s", "uAcpi", uacpi_status_to_string(ret));

			return;
		}

		terminal->debug("Preparing to enter S5...", "uAcpi");

		this->disableInts();

		terminal->debug("Entering S5...", "uAcpi");

		if (const uacpi_status ret = uacpi_enter_sleep_state(UACPI_SLEEP_STATE_S5); uacpi_unlikely_error(ret)) {
			terminal->error("Failed to enter S5: %s", "uAcpi", uacpi_status_to_string(ret));
		}
	}

	acpi_fadt *UAcpi::getFadtTable() const {
		return this->fadt;
	}

	acpi_madt *UAcpi::getMadtTable() const {
		return this->madt;
	}
}