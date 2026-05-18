#include "uacpi/UacpiKernAPI.hpp"

#include "hal/Interrupts.hpp"
#include "utils/Asm.hpp"
#include "hal/IOPort.hpp"
#include "Main.hpp"

#include "uacpi/utilities.h"

#include "uacpi/kernel_api.h"

// Interrupts

uacpi_interrupt_state uacpi_kernel_disable_interrupts() {
	u64 flags;

	asm volatile(
		"pushf\n"
		"pop %0\n"
		"cli"
		: "=r"(flags)
		:
		: "memory"
	);

	bool hadInts = flags & (1 << 9);

	if (hadInts) {
		kernel::x86_64::utils::Asm::cli();
	}

	return hadInts;
}

void uacpi_kernel_restore_interrupts(const uacpi_interrupt_state state) {
	if (state) {
		kernel::x86_64::utils::Asm::sti();
	}
}

namespace kernel::common::uacpi {
	using namespace x86_64;
	using namespace x86_64::hal;
	using namespace x86_64::utils;

	void UAcpi::archMiddleInit() {
		const uacpi_interrupt_model currInterruptModel = CpuManager::getCurrentCore()->apic.isInitialized() ?  UACPI_INTERRUPT_MODEL_IOAPIC : UACPI_INTERRUPT_MODEL_PIC;

		if (const uacpi_status ret = uacpi_set_interrupt_model(currInterruptModel); uacpi_unlikely_error(ret)) {
			CommonMain::getTerminal()->error("Failed to set interrupt model: %s", "uAcpi", uacpi_status_to_string(ret));
		}
	}

	void UAcpi::disableInts() {
		Asm::cli();
	}

	// I/O

	uacpi_status uacpiKernelIoRead8(uacpi_handle handle, uacpi_size offset, uacpi_u8 *outValue) {
		*outValue = IOPort::in8(reinterpret_cast<u64>(handle) + offset);

		return UACPI_STATUS_OK;
	}

	uacpi_status uacpiKernelIoRead16(uacpi_handle handle, uacpi_size offset, uacpi_u16 *outValue) {
		*outValue = IOPort::in16(reinterpret_cast<u64>(handle) + offset);

		return UACPI_STATUS_OK;
	}

	uacpi_status uacpiKernelIoRead32(uacpi_handle handle, uacpi_size offset, uacpi_u32 *outValue) {
		*outValue = IOPort::in32(reinterpret_cast<u64>(handle) + offset);

		return UACPI_STATUS_OK;
	}

	uacpi_status uacpiKernelIoWrite8(uacpi_handle handle, uacpi_size offset, uacpi_u8 inValue) {
		IOPort::out8(inValue, reinterpret_cast<u64>(handle) + offset);

		return UACPI_STATUS_OK;
	}

	uacpi_status uacpiKernelIoWrite16(uacpi_handle handle, uacpi_size offset, uacpi_u16 inValue) {
		IOPort::out16(inValue, reinterpret_cast<u64>(handle) + offset);

		return UACPI_STATUS_OK;
	}

	uacpi_status uacpiKernelIoWrite32(uacpi_handle handle, uacpi_size offset, uacpi_u32 inValue) {
		IOPort::out32(inValue, reinterpret_cast<u64>(handle) + offset);

		return UACPI_STATUS_OK;
	}

	// Interrupts

	uacpi_status uacpiKernelInstallInterruptHandler(uacpi_u32 irq, uacpi_interrupt_handler intHandler, uacpi_handle ctx, uacpi_handle *outIrqHandle) {
		return UACPI_STATUS_UNIMPLEMENTED;
	}

	uacpi_status uacpiKernelUninstallInterruptHandler(uacpi_interrupt_handler intHandler, uacpi_handle irqHandle) {
		return UACPI_STATUS_UNIMPLEMENTED;
	}

	// Events

	uacpi_interrupt_ret handlerPowerBtn(uacpi_handle ctx) {
		reinterpret_cast<Kernel *>(CommonMain::getInstance())->shutdown();

		return UACPI_INTERRUPT_HANDLED;
	}
}