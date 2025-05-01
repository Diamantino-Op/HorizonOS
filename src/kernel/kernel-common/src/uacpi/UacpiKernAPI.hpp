#ifndef KERNEL_X86_64_UACPIKERNAPI_HPP
#define KERNEL_X86_64_UACPIKERNAPI_HPP

#include "Types.hpp"
#include "uacpi/types.h"

namespace kernel::common::uacpi {
    class UAcpi {
    public:
        UAcpi() = default;
        ~UAcpi() = default;

        void init();
        void shutdown();

    private:
        void disableInts();
    };

    // I/O

    uacpi_status uacpiKernelIoRead8(uacpi_handle handle, uacpi_size offset, uacpi_u8 *out_value);
    uacpi_status uacpiKernelIoRead16(uacpi_handle handle, uacpi_size offset, uacpi_u16 *out_value);
    uacpi_status uacpiKernelIoRead32(uacpi_handle handle, uacpi_size offset, uacpi_u32 *out_value);

    uacpi_status uacpiKernelIoWrite8(uacpi_handle handle, uacpi_size offset, uacpi_u8 in_value);
    uacpi_status uacpiKernelIoWrite16(uacpi_handle handle, uacpi_size offset, uacpi_u16 in_value);
    uacpi_status uacpiKernelIoWrite32(uacpi_handle handle, uacpi_size offset, uacpi_u32 in_value);

    // Interrupts

    uacpi_status uacpiKernelInstallInterruptHandler(uacpi_u32 irq, uacpi_interrupt_handler intHandler, uacpi_handle ctx, uacpi_handle *out_irq_handle);
    uacpi_status uacpiKernelUninstallInterruptHandler(uacpi_interrupt_handler intHandler, uacpi_handle irq_handle);

    // Events

    uacpi_interrupt_ret handlerPowerBtn(uacpi_handle ctx);
}

#endif