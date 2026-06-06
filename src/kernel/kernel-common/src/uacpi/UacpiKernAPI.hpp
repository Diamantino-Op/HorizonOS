#ifndef KERNEL_COMMON_UACPIKERNAPI_HPP
#define KERNEL_COMMON_UACPIKERNAPI_HPP

#include "Types.hpp"
#include "uacpi/acpi.h"

namespace kernel::common::uacpi {
    struct IRQOut {
        u64 *irqHandlerAddr {};
        u8 number {};
    };

    class UAcpi {
    public:
        UAcpi() = default;
        ~UAcpi() = default;

        void earlyInit();

        auto getFadtTable() const -> acpi_fadt *;
        auto getMadtTable() const -> acpi_madt *;

        auto getIoApics() const -> acpi_madt_ioapic *;
        auto getIoApicsAmount() const -> u64;

        auto getIsos() const -> acpi_madt_interrupt_source_override *;
        auto getIsosAmount() const -> u64;

    private:
        u64 *earlyInitTablePtr {};

        acpi_fadt *fadt {};
        acpi_madt *madt {};

        acpi_madt_ioapic *ioApics {};
        acpi_madt_interrupt_source_override *isos {};

        u64 ioApicsAmount {};
        u64 isosAmount {};
    };
}

#endif