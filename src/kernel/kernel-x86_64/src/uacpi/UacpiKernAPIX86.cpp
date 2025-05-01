#include "uacpi/UacpiKernAPI.hpp"

#include "hal/Interrupts.hpp"
#include "utils/Asm.hpp"
#include "hal/IOPort.hpp"
#include "Main.hpp"

namespace kernel::common::uacpi {
	using namespace x86_64;
	using namespace x86_64::hal;
	using namespace x86_64::utils;

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
		if (Interrupts::getHandler(irq + 0x20)->fun != nullptr) {
			return UACPI_STATUS_ALREADY_EXISTS;
		}

		Interrupts::setHandler(irq + 0x20, reinterpret_cast<HandlerFun>(intHandler), static_cast<u64 *>(ctx));

		*outIrqHandle = Interrupts::getHandler(irq + 0x20);

		reinterpret_cast<Kernel *>(CommonMain::getInstance())->getDualPic()->unmask(irq + 0x20);

		return UACPI_STATUS_OK;
	}

	uacpi_status uacpiKernelUninstallInterruptHandler(uacpi_interrupt_handler intHandler, uacpi_handle irqHandle) {
		if (auto *handler = static_cast<IsrHandler *>(irqHandle); handler->fun != nullptr && handler->fun == reinterpret_cast<HandlerFun>(intHandler)) {
			handler->fun = nullptr;
			handler->ctx = nullptr;

			return UACPI_STATUS_OK;
		}

		// TODO: Mask Interrupt

		return UACPI_STATUS_NOT_FOUND;
	}

	void UAcpi::disableInts() {
		Asm::cli();
	}

	// Events

	uacpi_interrupt_ret handlerPowerBtn(uacpi_handle ctx) {
		reinterpret_cast<Kernel *>(CommonMain::getInstance())->shutdown();

		return UACPI_INTERRUPT_HANDLED;
	}
}